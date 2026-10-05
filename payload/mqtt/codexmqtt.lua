module(..., package.seeall)

local socket = require("socket")
local system = require("system")
local json = require("json")
local localcore = require("tasks.codex.localcore")
local CONFIG = "/data/codexmqtt/config.json"
local MANIFEST = "/data/codexmqtt/discovery.json"
local STATUS = "/tmp/harmony-operations/mqtt-status.json"
local mqttTask, moduleObj, writeError
local stopRequested, packetId = false, 0
local pending = {}

-- Lua 5.1 cannot yield through pcall/xpcall. Forward the scheduler's yields
-- while containing a failed integration task, without logging its credentials.
local function protected(fn, ...)
  local thread = coroutine.create(fn)
  local values = {coroutine.resume(thread, ...)}
  while values[1] and coroutine.status(thread) ~= "dead" do
    values = {coroutine.resume(thread, coroutine.yield(unpack(values, 2)))}
  end
  return unpack(values)
end

local function read(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local raw = f:read("*a"); f:close(); return raw
end
local function decode(raw)
  local ok, value = pcall(json.decode, raw or "")
  return ok and type(value) == "table" and value or nil
end
local function write(path, value)
  local raw = json.encode(value)
  if path == MANIFEST and os.execute("/data/codex/bin/codex_webui --space /data " .. tostring(#raw * 2) .. " >/dev/null 2>&1") ~= 0 then return nil end
  local f = io.open(path .. ".new", "w")
  if not f then return nil end
  local ok = f:write(raw); f:close()
  if not ok then return nil end
  return os.rename(path .. ".new", path)
end
local function runtime(state, err)
  write(STATUS, {state = state, error = err, updatedAt = os.time()})
end
local function readConfig()
  local raw = read(CONFIG)
  local cfg = decode(raw) or {enabled = false}
  cfg.broker = type(cfg.broker) == "table" and cfg.broker or {}
  cfg.baseTopic = cfg.baseTopic or "harmony/hub"
  cfg.discoveryPrefix = cfg.discoveryPrefix or "homeassistant"
  cfg.clientId = cfg.clientId or "harmony-codexmqtt"
  cfg.name = cfg.name or "Harmony Hub"
  cfg.keepAlive = math.max(10, math.min(300, tonumber(cfg.keepAlive) or 60))
  cfg.pollSeconds = math.max(2, math.min(60, tonumber(cfg.pollSeconds) or 10))
  cfg.haDiscovery = cfg.haDiscovery ~= false
  cfg.raw = raw
  for _, key in ipairs({"baseTopic", "discoveryPrefix", "clientId"}) do
    if type(cfg[key]) ~= "string" or #cfg[key] == 0 or #cfg[key] > 128 or cfg[key]:find("[%z+#]") then cfg.enabled = false end
  end
  return cfg
end
local function u16(n) return string.char(math.floor(n / 256) % 256, n % 256) end
local function utf(s) return u16(#s) .. s end
local function packet(kind, body)
  local n, bytes = #body, {}
  repeat
    local digit = n % 128; n = math.floor(n / 128)
    bytes[#bytes + 1] = string.char(digit + (n > 0 and 128 or 0))
  until n == 0
  return string.char(kind) .. table.concat(bytes) .. body
end

-- LuaSocket returns the last byte index, including for a partial write.
local function sendAll(sock, raw)
  if writeError then return nil, writeError end
  local pos = 1
  for _ = 1, 80 do
    local sent, err, partial = sock:send(raw, pos)
    pos = (sent or partial or (pos - 1)) + 1
    if pos > #raw then return true end
    if err ~= "timeout" then writeError = err or "short write"; return nil, writeError end
    system.sleep(25)
  end
  writeError = "MQTT write timed out"; return nil, writeError
end
local function recvBytes(sock, n, idle)
  local chunks, got = {}, 0
  for _ = 1, 40 do
    local data, err, partial = sock:receive(n - got)
    data = data or partial
    if data and #data > 0 then chunks[#chunks + 1] = data; got = got + #data end
    if got == n then return table.concat(chunks) end
    if err ~= "timeout" then return nil, err or "short read" end
    if idle and got == 0 then return nil, "timeout" end
    system.sleep(25)
  end
  return nil, "incomplete packet"
end
local function recvPacket(sock)
  local h, err = recvBytes(sock, 1, true)
  if not h then return nil, err end
  local multiplier, length = 1, 0
  for i = 1, 4 do
    local b; b, err = recvBytes(sock, 1)
    if not b then return nil, err == "timeout" and "incomplete packet" or err end
    local byte = b:byte()
    length = length + byte % 128 * multiplier
    if length > 65536 then return nil, "packet too large" end
    if byte < 128 then break end
    if i == 4 then return nil, "invalid packet length" end
    multiplier = multiplier * 128
  end
  local body = ""
  if length > 0 then
    body, err = recvBytes(sock, length)
    if not body then return nil, err == "timeout" and "incomplete packet" or err end
  end
  return {kind = math.floor(h:byte() / 16), flags = h:byte() % 16, body = body}
end
local function publish(sock, topic, payload, retain)
  return sendAll(sock, packet(retain and 0x31 or 0x30, utf(topic) .. payload))
end
local function connectMqtt(cfg)
  writeError = nil
  local sock, err = socket.tcp()
  if not sock then return nil, err end
  local host = cfg.broker.host
  if type(host) ~= "string" or not host:match("^%d+%.%d+%.%d+%.%d+$") then
    sock:close(); return nil, "Enter the broker IPv4 address"
  end
  sock:settimeout(0)
  local ok; ok, err = sock:connect(host, tonumber(cfg.broker.port) or 1883)
  if not ok and err == "timeout" then
    for _ = 1, 80 do
      system.sleep(25)
      if sock:getpeername() then ok = true; break end
    end
  end
  if not ok then sock:close(); return nil, err end
  local flags = 0x26 -- Clean session; retained offline will.
  local payload = utf(cfg.clientId) .. utf(cfg.baseTopic .. "/status") .. utf("offline")
  if type(cfg.broker.username) == "string" and cfg.broker.username ~= "" then
    flags = flags + 0x80; payload = payload .. utf(cfg.broker.username)
    if type(cfg.broker.password) == "string" then flags = flags + 0x40; payload = payload .. utf(cfg.broker.password) end
  end
  ok, err = sendAll(sock, packet(0x10, utf("MQTT") .. string.char(4, flags) .. u16(cfg.keepAlive) .. payload))
  if not ok then sock:close(); return nil, err end
  for _ = 1, 80 do
    local ack; ack, err = recvPacket(sock)
    if ack then
      if ack.kind == 2 and ack.flags == 0 and ack.body == "\0\0" then return sock end
      sock:close(); return nil, "Broker refused MQTT connection"
    end
    if err ~= "timeout" then break end
    system.sleep(25)
  end
  sock:close(); return nil, err or "CONNACK timed out"
end
local function safeId(s) return (s:lower():gsub("[^%w_%-]+", "_"):gsub("_+", "_")) end
local function hex(s) return (tostring(s):gsub(".", function(c) return string.format("%02x", c:byte()) end)) end
local function activities()
  local config = decode(read("/data/codex/local/config.json")) or {}
  return type(config.activities) == "table" and config.activities or {}
end
local function activityLabel(a) return a.name .. " (" .. a.id .. ")" end
local function findActivity(value)
  value = tostring(value)
  if value == "PowerOff" or value == "-1" or value == "power-off" then return "-1" end
  local found
  for _, a in ipairs(activities()) do
    if value == a.id or value == activityLabel(a) then return a.id end
    if value == a.name then if found then return nil end; found = a.id end
  end
  return found
end
local function statePayload(cfg, inventory)
  local state = decode(read("/tmp/harmony-operations/activity-state.json")) or {}
  local id, label = state.activityId or "", ""
  if id == "-1" or id == "power-off" then label = "PowerOff"
  else for _, a in ipairs(activities()) do if a.id == id then label = activityLabel(a) end end end
  local ip = system.getNetworkAttribute("ipaddr")
  return json.encode({activityId = id, activity = label, stateEstimated = true,
    firmware = system.getFirmwareVersion(), ip = ip, hostname = system.getHostName(),
    webui = "http://" .. tostring(ip) .. ":8080/", mqttClientId = cfg.clientId,
    deviceCount = inventory.deviceCount, commandCount = inventory.totalCommandCount})
end
local function discovery(cfg, inventory)
  local out, ident = {}, safeId(cfg.clientId)
  if not cfg.haDiscovery then return out end
  local ip = system.getNetworkAttribute("ipaddr")
  local hub = {identifiers = {ident}, name = cfg.name, manufacturer = "Logitech", model = "Harmony Hub",
    sw_version = system.getFirmwareVersion(), configuration_url = "http://" .. tostring(ip) .. ":8080/"}
  local function add(component, id, payload, device)
    payload.unique_id = id
    payload.device = device or hub
    payload.origin = {name = "Harmony Local", sw_version = "1.0.0"}
    payload.availability_topic = cfg.baseTopic .. "/status"
    out[cfg.discoveryPrefix .. "/" .. component .. "/" .. id .. "/config"] = json.encode(payload)
  end
  local options = {"PowerOff"}
  for _, a in ipairs(activities()) do options[#options + 1] = activityLabel(a) end
  add("select", ident .. "_activity", {name = "Activity", options = options,
    command_topic = cfg.baseTopic .. "/activity/set", state_topic = cfg.baseTopic .. "/state",
    value_template = "{{ value_json.activity or 'None' }}", icon = "mdi:remote-tv"})
  add("button", ident .. "_power_off", {name = "Stop activity", payload_press = "PowerOff",
    command_topic = cfg.baseTopic .. "/activity/set", icon = "mdi:power"})
  for _, s in ipairs({{"activity_id", "Activity ID", "activityId", "mdi:identifier"},
    {"ip", "IP Address", "ip", "mdi:ip-network"}, {"firmware", "Firmware", "firmware", "mdi:chip"},
    {"ir_devices", "IR Devices", "deviceCount", "mdi:remote"}, {"ir_commands", "IR Commands", "commandCount", "mdi:counter"}}) do
    add("sensor", ident .. "_" .. s[1], {name = s[2], state_topic = cfg.baseTopic .. "/state",
      value_template = "{{ value_json." .. s[3] .. " }}", icon = s[4], entity_category = "diagnostic"})
  end
  local common = {power=true, powertoggle=true, poweron=true, poweroff=true, volumeup=true, volumedown=true,
    volup=true, voldown=true, mute=true, input=true, play=true, pause=true, stop=true, up=true, down=true,
    left=true, right=true, ok=true, select=true, home=true, back=true}
  for _, d in ipairs(inventory.devices or {}) do
    local id = ident .. "_device_" .. hex(d.id)
    local device = {identifiers = {id}, name = d.name, manufacturer = d.manufacturer, model = d.model, via_device = ident}
    for _, c in ipairs(d.commands or {}) do
      add("button", id .. "_" .. hex(c.name), {name = c.name, icon = "mdi:remote",
        command_topic = cfg.baseTopic .. "/command", payload_press = json.encode({deviceId = d.id, command = c.name}),
        enabled_by_default = common[c.name:lower():gsub("[^%w]", "")] == true}, device)
    end
  end
  return out
end
local function publishDiscovery(sock, cfg, inventory, previous, force, owner)
  local desired = discovery(cfg, inventory)
  if owner then
    local union, changed = {}, false
    for t in pairs(previous) do union[t] = true; if not desired[t] then changed = true end end
    for t in pairs(desired) do union[t] = true; if not previous[t] then changed = true end end
    if changed then
      local names = {}; for t in pairs(union) do names[#names + 1] = t end
      -- Remember new topics before publishing: a crash must not leave undiscoverable orphans.
      if not write(MANIFEST, {owner = owner, topics = names}) then writeError = "Cannot save discovery manifest"; return nil end
    end
  end
  for topic in pairs(previous or {}) do if not desired[topic] then publish(sock, topic, "", true) end end
  for topic, payload in pairs(desired) do
    if force or not previous or previous[topic] ~= payload then publish(sock, topic, payload, true) end
  end
  return desired
end
local function result(sock, cfg, payload)
  if payload.operation and payload.operation.id then pending[payload.operation.id] = os.time() end
  return publish(sock, cfg.baseTopic .. "/result", json.encode(payload), false)
end
local function handlePublish(sock, cfg, pkt, ready)
  local body, qos = pkt.body, math.floor(pkt.flags / 2) % 4
  if #body < 2 or qos ~= 0 then return nil, "Unexpected MQTT PUBLISH (QoS 0 required)" end
  local n = body:byte(1) * 256 + body:byte(2)
  if n == 0 or n > #body - 2 then return nil, "Invalid MQTT topic" end
  local topic, payload = body:sub(3, n + 2), body:sub(n + 3)
  if topic == cfg.discoveryPrefix .. "/status" then return payload == "online" and "discover" or true end
  if topic ~= cfg.baseTopic .. "/activity/set" and topic ~= cfg.baseTopic .. "/command" and topic ~= cfg.baseTopic .. "/hbus" then return true end
  if pkt.flags % 2 == 1 then return true end -- Never replay a retained physical command.
  if not ready then result(sock, cfg, {ok = false, error = "Local service is not ready"}); return true end
  if #payload > 2048 then result(sock, cfg, {ok = false, error = "Command exceeds 2048 bytes"}); return true end
  local count = 0; for _ in pairs(pending) do count = count + 1 end
  if count >= 32 then result(sock, cfg, {ok = false, error = "Too many outstanding operations"}); return true end
  local command = decode(payload)
  if topic == cfg.baseTopic .. "/hbus" or (command and command.cmd) then
    result(sock, cfg, {ok = false, error = "Raw HBus commands are disabled"}); return true
  end
  local activity = topic == cfg.baseTopic .. "/activity/set" and payload or command and (command.activityId or command.activity)
  local op, err
  if activity then
    local id = findActivity(activity)
    if id then op, err = localcore.request("/api/v1/activities/run", {activityId = id})
    else err = "Unknown or ambiguous local activity" end
  elseif command and type(command.command) == "string" and (type(command.deviceId) == "string" or type(command.deviceId) == "number") then
    if command.mode and command.mode ~= "tap" then err = "MQTT supports taps; use the paired API for holds"
    else op, err = localcore.command(command) end
  else err = "Expected deviceId and command, or activityId" end
  result(sock, cfg, {ok = op ~= nil, operation = op, error = err})
  return true
end
local function publishOperations(sock, cfg)
  for id, created in pairs(pending) do
    local op = decode(read("/tmp/harmony-operations/" .. id .. ".json"))
    if op and (op.state == "completed" or op.state == "failed" or op.state == "cancelled") then
      if result(sock, cfg, {operationId = id, state = op.state, result = op.result}) then pending[id] = nil end
    elseif os.time() - created > 180 then
      if result(sock, cfg, {operationId = id, state = "unknown", error = "Operation history expired"}) then pending[id] = nil end
    end
  end
end
local function runConnection(sock, cfg)
  packetId = packetId % 65535 + 1
  local topics = {cfg.baseTopic .. "/command", cfg.baseTopic .. "/activity/set", cfg.baseTopic .. "/hbus", cfg.discoveryPrefix .. "/status"}
  local body = u16(packetId)
  for _, topic in ipairs(topics) do body = body .. utf(topic) .. "\0" end
  sendAll(sock, packet(0x82, body))
  local started, lastPing, lastPoll = os.time(), os.time(), 0
  local pingAt, subscribed, ready, force = nil, false, false, true
  local previous, stateCache = {}, ""
  local manifest = decode(read(MANIFEST)) or {}
  -- Only retire discovery that this exact client previously owned on this broker.
  local owner = cfg.broker.host .. ":" .. tostring(cfg.broker.port or 1883) .. "/" .. cfg.clientId
  if manifest.owner == owner and type(manifest.topics) == "table" then
    for _, t in ipairs(manifest.topics) do if type(t) == "string" then previous[t] = true end end
  end
  while not stopRequested and not writeError do
    local pkt, err = recvPacket(sock)
    if pkt then
      if pkt.kind == 9 then
        if pkt.flags ~= 0 or pkt.body ~= u16(packetId) .. string.rep("\0", #topics) then return nil, "Broker refused command subscription" end
        subscribed = true
      elseif pkt.kind == 13 then
        if pkt.flags ~= 0 or #pkt.body ~= 0 then return nil, "Invalid PINGRESP" end
        pingAt = nil
      elseif pkt.kind == 3 then
        local ok; ok, err = handlePublish(sock, cfg, pkt, ready)
        if not ok then return nil, err end
        if ok == "discover" then force = true end
      else return nil, "Unexpected MQTT packet" end
    elseif err ~= "timeout" then return nil, err end
    local now = os.time()
    if not subscribed and now - started > 5 then return nil, "SUBACK timed out" end
    if pingAt and now - pingAt >= math.floor(cfg.keepAlive / 2) then return nil, "PINGRESP timed out" end
    if now - lastPing >= math.floor(cfg.keepAlive / 2) and not pingAt then
      sendAll(sock, packet(0xC0, "")); pingAt, lastPing = now, now
    end
    if subscribed and (force or now - lastPoll >= cfg.pollSeconds) then
      if read(CONFIG) ~= cfg.raw then return true end
      local inventory, why = localcore.request("/api/v1/devices")
      if inventory then
        local nextTopics = publishDiscovery(sock, cfg, inventory, previous, force, owner)
        if not nextTopics then return nil, writeError end
        local changed = false
        for t in pairs(previous) do if not nextTopics[t] then changed = true end end
        for t in pairs(nextTopics) do if not previous[t] then changed = true end end
        if not writeError then
          if changed then
            local names = {}; for t in pairs(nextTopics) do names[#names + 1] = t end
            -- Manifest is written only when the topic set changes, not every poll.
            if not write(MANIFEST, {owner = owner, topics = names}) then return nil, "Cannot save discovery manifest" end
          end
          previous = nextTopics
          local state = statePayload(cfg, inventory)
          if force or state ~= stateCache then if publish(sock, cfg.baseTopic .. "/state", state, true) then stateCache = state end end
          if not ready then publish(sock, cfg.baseTopic .. "/status", "online", true) end
          ready = true; runtime("connected")
        end
      else
        publish(sock, cfg.baseTopic .. "/status", "offline", true)
        ready = false; runtime("waiting_for_hub", why)
      end
      force = false; lastPoll = now
    end
    publishOperations(sock, cfg)
    system.sleep(50)
  end
  return not writeError, writeError
end
local function mqttLoop()
  while not stopRequested do
    local cfg = readConfig()
    if not cfg.enabled then runtime("disabled"); system.sleep(5000)
    else
      runtime("connecting")
      local sock, err = connectMqtt(cfg)
      if sock then
        local called, ok, why = protected(runConnection, sock, cfg)
        err = called and why or "MQTT session failed"
        -- Failed sessions close without DISCONNECT so the broker emits its will.
        if called and ok and publish(sock, cfg.baseTopic .. "/status", "offline", true) then sendAll(sock, packet(0xE0, "")) end
        sock:close()
      end
      runtime("disconnected", err)
      system.sleep(15000)
    end
  end
  mqttTask = nil
end
local function start()
  stopRequested = false
  localcore.start()
  if not mqttTask then mqttTask = system.addTask("codexmqtt", mqttLoop) end
  return true
end
function instance(self)
  if not moduleObj then moduleObj = setmetatable({}, {__index = self}) end
  return moduleObj
end
function discover(self)
  start()
  return {["codex-mqtt"] = {id = "codex-mqtt", type = "codexmqtt", name = "Harmony MQTT"}}
end
function pair(self) return start() end
function monitor(self)
  start()
  while not stopRequested do system.sleep(60000) end
end
function status(self) return decode(read(STATUS)) or {state = "stopped"} end
function exit(self) stopRequested = true; return true end
