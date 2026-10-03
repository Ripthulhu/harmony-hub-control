module(..., package.seeall)

local socket = require("socket")
local system = require("system")
local json = require("json")
local log = require("log").logger("auto.p.codexmqtt")

local localcore = require("tasks.codex.localcore")
local session = require("tasks.harmonywebservices.core.session")
local digest = require("tasks.harmonywebservices.core.statedigest"):instance()

local CONFIG_FILE = "/data/codexmqtt/config.json"
local IR_EVENT_LOG = "/data/codex/ir-events.log"
local DEFAULT_BASE_TOPIC = "harmony/hub"
local DEFAULT_DISCOVERY_PREFIX = "homeassistant"
local DEFAULT_CLIENT_ID = "harmony-codexmqtt"
local DEFAULT_KEEPALIVE = 60
local DEFAULT_DUPLICATE_WINDOW_MS = 3000
local IR_EVENT_MAX_BYTES = 65536

local moduleObj
local mqttTask
local stopRequested = false
local stateCache = ""
local pktid = 1
local recentCommandKey = nil
local recentCommandAt = 0
local pendingOperations = {}

local function new(self)
  local obj = {}
  setmetatable(obj, self)
  self.__index = self
  return obj
end

function instance(self)
  if not moduleObj then
    moduleObj = new(self)
  end
  return moduleObj
end

local function readFile(path)
  local f = io.open(path, "r")
  if not f then
    return nil
  end
  local data = f:read("*a")
  f:close()
  return data
end

local function readConfig()
  local data = readFile(CONFIG_FILE)
  if not data then
    return {enabled = false}
  end
  local ok, cfg = system.safeCall(json.decode, data)
  if not ok or type(cfg) ~= "table" then
    log.notice("invalid codexmqtt config")
    return {enabled = false}
  end
  cfg.broker = cfg.broker or {}
  cfg.baseTopic = cfg.baseTopic or DEFAULT_BASE_TOPIC
  cfg.discoveryPrefix = cfg.discoveryPrefix or DEFAULT_DISCOVERY_PREFIX
  cfg.clientId = cfg.clientId or DEFAULT_CLIENT_ID
  cfg.keepAlive = tonumber(cfg.keepAlive) or DEFAULT_KEEPALIVE
  cfg.pollSeconds = tonumber(cfg.pollSeconds) or 10
  cfg.duplicateWindowMs = tonumber(cfg.duplicateWindowMs) or DEFAULT_DUPLICATE_WINDOW_MS
  if cfg.duplicateWindowMs < 0 then
    cfg.duplicateWindowMs = 0
  elseif cfg.duplicateWindowMs > 60000 then
    cfg.duplicateWindowMs = 60000
  end
  cfg.name = cfg.name or system.getHostName() or "Harmony Hub"
  if cfg.haDiscovery == nil then
    cfg.haDiscovery = true
  end
  cfg.__raw = data
  return cfg
end

local function u16(n)
  return string.char(math.floor(n / 256) % 256, n % 256)
end

local function utf(s)
  s = tostring(s or "")
  return u16(#s) .. s
end

local function remLen(n)
  local out = ""
  repeat
    local digit = n % 128
    n = math.floor(n / 128)
    if n > 0 then
      digit = digit + 128
    end
    out = out .. string.char(digit)
  until n == 0
  return out
end

local function packet(kind, body)
  return string.char(kind) .. remLen(#body) .. body
end

local function nextPktid()
  pktid = pktid + 1
  if pktid > 65535 then
    pktid = 1
  end
  return pktid
end

local function nowMillis()
  local ok, value = system.safeCall(function()
    return system.jiffies():tomillis()
  end)
  if ok and value then
    return tonumber(value) or 0
  end
  return (tonumber(os.time()) or 0) * 1000
end

local function appendIrEvent(event, fields)
  local row = fields or {}
  row.event = event
  row.ts = tonumber(os.time()) or 0
  local ok, encoded = system.safeCall(json.encode, row)
  if not ok or not encoded then
    return
  end
  local existing = io.open(IR_EVENT_LOG, "r")
  if existing then
    local size = existing:seek("end") or 0
    existing:close()
    if size > IR_EVENT_MAX_BYTES then
      os.remove(IR_EVENT_LOG .. ".1")
      os.rename(IR_EVENT_LOG, IR_EVENT_LOG .. ".1")
    end
  end
  local f = io.open(IR_EVENT_LOG, "a")
  if not f then
    return
  end
  f:write(encoded, "\n")
  f:close()
end

local function holdActionDetails(params)
  if type(params) ~= "table" then
    return {}
  end
  local details = {
    status = tostring(params.status or ""),
    count = tostring(params.count or ""),
    delayInMs = tostring(params.delayInMs or "")
  }
  if type(params.action) == "string" then
    local ok, action = system.safeCall(json.decode, params.action)
    if ok and type(action) == "table" then
      details.deviceId = tostring(action.deviceId or "")
      details.command = tostring(action.command or "")
      details.type = tostring(action.type or "")
    end
  end
  return details
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
  if not h then
    return nil, err
  end
  local multiplier = 1
  local length = 0
  for i = 1, 4 do
    local b
    b, err = recvBytes(sock, 1)
    if not b then
      return nil, err == "timeout" and "incomplete packet" or err
    end
    local byte = string.byte(b, 1)
    length = length + (byte % 128) * multiplier
    if length > 65536 then return nil, "packet too large" end
    if byte < 128 then
      break
    end
    if i == 4 then return nil, "invalid packet length" end
    multiplier = multiplier * 128
  end
  local body = ""
  if length > 0 then
    body, err = recvBytes(sock, length)
    if not body or #body ~= length then
      return nil, err == "timeout" and "incomplete packet" or (err or "short read")
    end
  end
  return {kind = math.floor(string.byte(h, 1) / 16), flags = string.byte(h, 1) % 16, body = body}
end

local function publish(sock, topic, payload, retain)
  payload = tostring(payload or "")
  local fixed = retain and 0x31 or 0x30
  return sock:send(packet(fixed, utf(topic) .. payload))
end

local function subscribe(sock, topics)
  local id = nextPktid()
  local body = u16(id)
  for _, topic in ipairs(topics) do
    body = body .. utf(topic) .. string.char(0)
  end
  return sock:send(packet(0x82, body))
end

local function connectMqtt(cfg)
  local host = cfg.broker.host
  local port = tonumber(cfg.broker.port) or 1883
  local sock, err = socket.tcp()
  if not sock then
    return nil, err
  end
  if not host:match("^%d+%.%d+%.%d+%.%d+$") then sock:close(); return nil, "Use the broker IPv4 address to avoid blocking the local engine on DNS" end
  sock:settimeout(0)
  local ok
  ok, err = sock:connect(host, port)
  if not ok and err == "timeout" then
    for _ = 1, 80 do
      system.sleep(25)
      if sock:getpeername() then ok = true; break end
    end
  end
  if not ok then sock:close(); return nil, err end

  local flags = 0x02 + 0x04 + 0x20
  local payload = utf(cfg.clientId) .. utf(cfg.baseTopic .. "/status") .. utf("offline")
  if cfg.broker.username and cfg.broker.username ~= "" then
    flags = flags + 0x80
    if cfg.broker.password and cfg.broker.password ~= "" then
      flags = flags + 0x40
    end
  end
  if cfg.broker.username and cfg.broker.username ~= "" then
    payload = payload .. utf(cfg.broker.username)
    if cfg.broker.password and cfg.broker.password ~= "" then
      payload = payload .. utf(cfg.broker.password)
    end
  end

  local vh = utf("MQTT") .. string.char(4, flags) .. u16(cfg.keepAlive)
  sock:send(packet(0x10, vh .. payload))
  local ack
  for _ = 1, 40 do
    ack, err = recvPacket(sock)
    if ack or err ~= "timeout" then break end
    system.sleep(25)
  end
  if not ack or ack.kind ~= 2 or #ack.body ~= 2 or string.byte(ack.body, 2) ~= 0 then
    sock:close()
    return nil, err or "CONNACK failed"
  end
  sock:settimeout(0)
  return sock
end

local function safeId(s)
  s = tostring(s or "harmony_hub")
  s = string.lower(s)
  s = string.gsub(s, "[^%w_%-]+", "_")
  s = string.gsub(s, "_+", "_")
  return s
end

local function localActivities()
  local ok, config = pcall(json.decode, readFile("/data/codex/local/config.json") or "")
  return ok and config.activities or {}
end

local function activityName(account, id)
  if tostring(id) == "-1" then return "PowerOff" end
  for _, act in ipairs(localActivities()) do if act.id == tostring(id) then return act.name end end
  return tostring(id or "unknown")
end

local function activityOptions(account)
  local opts = {"PowerOff"}
  for _, act in ipairs(localActivities()) do opts[#opts + 1] = act.name end
  return opts
end

local function inventoryCounts(account)
  local devices = 0
  local commands = 0
  if account and account.devices then
    for _, device in pairs(account.devices) do
      devices = devices + 1
      if device.commands then
        for _, _ in pairs(device.commands) do
          commands = commands + 1
        end
      end
    end
  end
  return devices, commands
end

local function findActivity(account, nameOrId)
  local value = tostring(nameOrId or "")
  if value == "-1" or string.lower(value) == "off" or value == "PowerOff" then return "-1" end
  for _, act in ipairs(localActivities()) do if act.id == value or act.name == value then return act.id end end
  return nil
end

local function callEngine(cmd, params)
  return nil, "Raw HBus calls are disabled; use the local command API"
end

local function startActivity(activityId)
  return localcore.request("/api/v1/activities/run", {activityId = tostring(activityId)})
end

local function sendCommand(cmd)
  return localcore.command(cmd)
end

local function statePayload(cfg)
  local account = session.getAccount()
  local _, localState = pcall(json.decode, readFile("/tmp/harmony-operations/activity-state.json") or "")
  local id = type(localState) == "table" and localState.activityId or "-1"
  local deviceCount, commandCount = inventoryCounts(account)
  local ip = system.getNetworkAttribute("ipaddr")
  local payload = {
    activityId = tostring(id or "-1"),
    activity = activityName(account, id),
    activityStatus = 0,
    stateEstimated = true,
    stateVersion = digest.stateVersion or 0,
    firmware = system.getFirmwareVersion(),
    ip = ip,
    hostname = system.getHostName(),
    webui = ip and ("http://" .. tostring(ip) .. ":8080/") or "",
    mqttClientId = cfg.clientId,
    baseTopic = cfg.baseTopic,
    discoveryPrefix = cfg.discoveryPrefix,
    deviceCount = deviceCount,
    commandCount = commandCount
  }
  return json.encode(payload)
end

local function publishState(sock, cfg, force)
  local payload = statePayload(cfg)
  if force or payload ~= stateCache then
    stateCache = payload
    publish(sock, cfg.baseTopic .. "/state", payload, true)
  end
end

local function publishDiscovery(sock, cfg)
  if not cfg.haDiscovery then
    return
  end
  local account = session.getAccount()
  local ident = safeId(cfg.clientId)
  local base = cfg.baseTopic
  local ip = system.getNetworkAttribute("ipaddr")
  local dev = {
    identifiers = {ident},
    name = cfg.name,
    manufacturer = "Logitech",
    model = "Harmony Hub",
    sw_version = system.getFirmwareVersion(),
    configuration_url = ip and ("http://" .. tostring(ip) .. ":8080/") or nil
  }
  local origin = {
    name = "codexmqtt",
    sw_version = "0.3",
    support_url = "http://" .. tostring(ip or "harmony-hub.local") .. ":8080/"
  }
  local availability = base .. "/status"

  local selectPayload = {
    name = "Activity",
    unique_id = ident .. "_activity",
    command_topic = base .. "/activity/set",
    state_topic = base .. "/state",
    value_template = "{{ value_json.activity }}",
    json_attributes_topic = base .. "/state",
    options = activityOptions(account),
    availability_topic = availability,
    payload_available = "online",
    payload_not_available = "offline",
    icon = "mdi:remote-tv",
    origin = origin,
    device = dev
  }
  publish(sock, cfg.discoveryPrefix .. "/select/" .. ident .. "_activity/config", json.encode(selectPayload), true)

  local sensors = {
    {"activity_id", "Activity ID", "activityId", "mdi:identifier"},
    {"ip", "IP Address", "ip", "mdi:ip-network"},
    {"firmware", "Firmware", "firmware", "mdi:chip"},
    {"ir_devices", "IR Devices", "deviceCount", "mdi:remote"},
    {"ir_commands", "IR Commands", "commandCount", "mdi:counter"}
  }
  for _, sensor in ipairs(sensors) do
    local payload = {
      name = sensor[2],
      unique_id = ident .. "_" .. sensor[1],
      state_topic = base .. "/state",
      value_template = "{{ value_json." .. sensor[3] .. " }}",
      json_attributes_topic = sensor[1] == "ip" and (base .. "/state") or nil,
      availability_topic = availability,
      payload_available = "online",
      payload_not_available = "offline",
      icon = sensor[4],
      entity_category = "diagnostic",
      origin = origin,
      device = {identifiers = {ident}}
    }
    publish(sock, cfg.discoveryPrefix .. "/sensor/" .. ident .. "_" .. sensor[1] .. "/config", json.encode(payload), true)
  end

  local offPayload = {
    name = "Power Off",
    unique_id = ident .. "_power_off",
    command_topic = base .. "/activity/set",
    payload_press = "PowerOff",
    availability_topic = availability,
    payload_available = "online",
    payload_not_available = "offline",
    icon = "mdi:power",
    origin = origin,
    device = {identifiers = {ident}}
  }
  publish(sock, cfg.discoveryPrefix .. "/button/" .. ident .. "_power_off/config", json.encode(offPayload), true)
end

local function publishResult(sock, cfg, payload)
  if payload.operation and payload.operation.id then
    pendingOperations[payload.operation.id] = os.time()
  end
  publish(sock, cfg.baseTopic .. "/result", json.encode(payload), false)
end

local function publishOperations(sock, cfg)
  for id, created in pairs(pendingOperations) do
    local ok, operation = pcall(json.decode, readFile("/tmp/harmony-operations/" .. id .. ".json") or "")
    if ok and type(operation) == "table" and (operation.state == "completed" or operation.state == "failed" or operation.state == "cancelled") then
      publish(sock, cfg.baseTopic .. "/result", json.encode({operationId = id, state = operation.state, result = operation.result}), false)
      pendingOperations[id] = nil
    elseif os.time() - created > 180 then
      publish(sock, cfg.baseTopic .. "/result", json.encode({operationId = id, state = "unknown", error = "Operation history expired"}), false)
      pendingOperations[id] = nil
    end
  end
end

local function handleActivity(sock, cfg, payload)
  log.notice("codexmqtt activity request", tostring(payload or ""))
  local account = session.getAccount()
  local activityId = findActivity(account, payload)
  if not activityId then
    publishResult(sock, cfg, {ok = false, error = "unknown activity", value = payload})
    return
  end
  local reply, err = startActivity(activityId)
  publishResult(sock, cfg, {ok = reply ~= nil, cmd = "startactivity", activityId = activityId, operation = reply, error = err})
end

local function handleCommand(sock, cfg, payload, topic)
  local ok, decoded = system.safeCall(json.decode, payload)
  if ok and type(decoded) == "table" then
    if decoded.cmd then
      log.notice("codexmqtt hbus command", tostring(decoded.cmd), "topic", tostring(topic or ""))
      local reply, err = callEngine(decoded.cmd, decoded.params or {})
      if tostring(decoded.cmd) == "harmony.engine?holdaction" then
        local details = holdActionDetails(decoded.params)
        details.source = "mqtt-hbus"
        details.topic = tostring(topic or "")
        details.reply = tostring(reply or "")
        appendIrEvent("ir_send", details)
      end
      publishResult(sock, cfg, {ok = reply ~= nil, cmd = decoded.cmd, reply = reply, error = err})
    elseif decoded.activity or decoded.activityId then
      handleActivity(sock, cfg, decoded.activityId or decoded.activity)
    elseif decoded.deviceId and decoded.command then
      log.notice("codexmqtt ir command", "device", tostring(decoded.deviceId), "command", tostring(decoded.command), "topic", tostring(topic or ""))
      local reply, err = sendCommand(decoded)
      appendIrEvent("ir_send", {
        source = "mqtt",
        topic = tostring(topic or ""),
        deviceId = tostring(decoded.deviceId or ""),
        command = tostring(decoded.command or ""),
        status = tostring(decoded.status or "pressrelease"),
        count = tostring(decoded.count or 1),
        delayInMs = tostring(decoded.delayInMs or ""),
        reply = tostring(reply or "")
      })
      publishResult(sock, cfg, {ok = reply ~= nil, cmd = "command", operation = reply, error = err})
    else
      publishResult(sock, cfg, {ok = false, error = "unknown command shape"})
    end
    return
  end

  local activity = string.match(payload or "", "^activity:(.+)$")
  if activity then
    handleActivity(sock, cfg, activity)
    return
  end
  publishResult(sock, cfg, {ok = false, error = "payload must be JSON or activity:<name>"})
end

local function handlePublish(sock, cfg, pkt)
  local body = pkt.body
  if #body < 2 then
    return
  end
  local topicLen = string.byte(body, 1) * 256 + string.byte(body, 2)
  local topic = string.sub(body, 3, 2 + topicLen)
  local pos = 3 + topicLen
  local qos = math.floor(pkt.flags / 2) % 4
  if qos > 0 then
    pos = pos + 2
  end
  local payload = string.sub(body, pos)
  local retain = (pkt.flags % 2) == 1
  local isCommandTopic = topic == cfg.baseTopic .. "/activity/set" or topic == cfg.baseTopic .. "/command" or topic == cfg.baseTopic .. "/hbus"

  if retain and isCommandTopic then
    log.notice("codexmqtt ignored retained command", tostring(topic))
    appendIrEvent("ir_ignored", {source = "mqtt", reason = "retained command", topic = tostring(topic or "")})
    publishResult(sock, cfg, {ok = false, ignored = true, reason = "retained command", topic = topic})
    return
  end

  if isCommandTopic then
    local key = topic .. "\n" .. tostring(payload or "")
    local now = nowMillis()
    if cfg.duplicateWindowMs > 0 and recentCommandKey == key and now >= recentCommandAt and now - recentCommandAt < cfg.duplicateWindowMs then
      log.notice("codexmqtt ignored duplicate command", tostring(topic))
      appendIrEvent("ir_ignored", {source = "mqtt", reason = "duplicate command", topic = tostring(topic or ""), windowMs = tostring(cfg.duplicateWindowMs)})
      publishResult(sock, cfg, {ok = false, ignored = true, reason = "duplicate command", topic = topic})
      return
    end
    recentCommandKey = key
    recentCommandAt = now
  end

  if topic == cfg.baseTopic .. "/activity/set" then
    handleActivity(sock, cfg, payload)
  elseif topic == cfg.baseTopic .. "/command" or topic == cfg.baseTopic .. "/hbus" then
    handleCommand(sock, cfg, payload, topic)
  elseif topic == cfg.discoveryPrefix .. "/status" and payload == "online" then
    publishDiscovery(sock, cfg)
    publishState(sock, cfg, true)
  end
end

local function mqttLoop()
  while not stopRequested do
    local cfg = readConfig()
    if not cfg.enabled or not cfg.broker or not cfg.broker.host then
      log.notice("codexmqtt disabled or missing broker host")
      system.sleep(5000)
    else
      local sock, err = connectMqtt(cfg)
      if not sock then
        log.notice("codexmqtt connect failed:", err)
        system.sleep(15000)
      else
        log.notice("codexmqtt connected to", cfg.broker.host)
        publish(sock, cfg.baseTopic .. "/status", "online", true)
        publishDiscovery(sock, cfg)
        publishState(sock, cfg, true)
        subscribe(sock, {
          cfg.baseTopic .. "/activity/set",
          cfg.baseTopic .. "/command",
          cfg.baseTopic .. "/hbus",
          cfg.discoveryPrefix .. "/status"
        })

        local lastPing = os.time()
        local lastPoll = 0
        local lastConfigCheck = 0
        local cfgRaw = cfg.__raw or ""
        while not stopRequested do
          local pkt, perr = recvPacket(sock)
          if pkt and pkt.kind == 3 then
            local ok, herr = system.safeCall(handlePublish, sock, cfg, pkt)
            if not ok then
              log.notice("codexmqtt publish handler failed:", tostring(herr))
            end
          elseif pkt and pkt.kind == 13 then
          elseif pkt then
          elseif perr and perr ~= "timeout" then
            log.notice("codexmqtt receive failed:", perr)
            break
          end

          local now = os.time()
          publishOperations(sock, cfg)
          if now - lastPoll >= cfg.pollSeconds then
            publishState(sock, cfg, false)
            lastPoll = now
          end
          if now - lastConfigCheck >= 5 then
            local nextRaw = readFile(CONFIG_FILE) or ""
            if nextRaw ~= cfgRaw then
              log.notice("codexmqtt config changed; reconnecting")
              break
            end
            lastConfigCheck = now
          end
          if now - lastPing >= math.floor(cfg.keepAlive / 2) then
            local ok, serr = sock:send(packet(0xC0, ""))
            if not ok then
              log.notice("codexmqtt ping failed:", serr)
              break
            end
            lastPing = now
          end
          system.sleep(100)
        end
        pcall(function() publish(sock, cfg.baseTopic .. "/status", "offline", true) end)
        pcall(function() sock:send(packet(0xE0, "")) end)
        pcall(function() sock:close() end)
        system.sleep(5000)
      end
    end
  end
  mqttTask = nil
end

local function start()
  stopRequested = false
  localcore.start()
  if not mqttTask then
    mqttTask = system.addTask("codexmqtt", mqttLoop)
  end
  return true
end

function discover(self)
  start()
  return {
    ["codex-mqtt"] = {
      id = "codex-mqtt",
      type = "codexmqtt",
      name = "Codex MQTT Bridge"
    }
  }
end

function pair(self, gatewayId, gateway)
  return start()
end

function monitor(self)
  start()
  while not stopRequested do
    system.sleep(60000)
  end
end

function status(self)
  return {state = mqttTask and "running" or "stopped", config = CONFIG_FILE}
end

function exit(self)
  stopRequested = true
  return true
end
