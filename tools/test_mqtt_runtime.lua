-- Run from the repo root with Lua 5.1; no broker or device connection is made.
local tasks, files, broadcasts = {}, {}, 0
local flag = "/data/codex/reload_resources"
local sys = {
  safeCall = pcall,
  addTask = function(name, fn) tasks[name] = fn; return name end,
  sleep = function(ms) coroutine.yield(ms) end,
  broadcastMessage = function(name, resources)
    assert(name == "config_new" and #resources > 0)
    assert(files[flag .. ".loading"])
    broadcasts = broadcasts + 1
  end
}
package.loaded.socket = {}
package.loaded.system = sys
package.loaded.json = {decode = function() return {enabled = false} end}
package.loaded.log = {logger = function() return {notice = function() end} end}
package.loaded["tasks.connectserver.core.engine"] = {}
package.loaded["tasks.harmonywebservices.core.session"] = {getAccount = function() return {} end}
package.loaded["tasks.harmonywebservices.core.statedigest"] = {instance = function() return {} end}
local core = assert(loadfile("payload/core/localcore.lua"))()
package.loaded["tasks.codex.localcore"] = core
assert(loadfile("payload/mqtt/codexmqtt.lua"))("test_bridge")
test_bridge.discover()
assert(tasks.codexmqtt and tasks["harmony-local-resources"] and tasks["harmony-local-activities"])

local function upvalue(fn, name)
  for i = 1, 50 do
    local key, value = debug.getupvalue(fn, i)
    if key == name then return value end
  end
  error("missing upvalue " .. name)
end
local connection = upvalue(tasks.codexmqtt, "runConnection")
local recv = upvalue(connection, "recvPacket")
local function packet(parts, expected)
  local index = 0
  local sock = {receive = function()
    index = index + 1
    return unpack(parts[index] or {nil, "timeout", ""})
  end}
  local result, err
  local worker = coroutine.create(function() result, err = recv(sock) end)
  while coroutine.status(worker) ~= "dead" do assert(coroutine.resume(worker)) end
  if expected then assert(not result and err == expected, tostring(err))
  else assert(result and result.kind == 2 and result.body == "\0\0") end
end
packet({{"\32"}, {"\2"}, {"\0\0"}})
packet({{nil, "closed", ""}}, "closed")
packet({{"\32"}, {nil, "closed", ""}}, "closed")
packet({{"\32"}, {"\2"}, {nil, "timeout", "\0"}}, "incomplete packet")
packet({{"\32"}, {nil, "timeout", ""}}, "incomplete packet")
packet({{nil, "timeout", ""}}, "timeout")
packet({{"\32"}, {"\255"}, {"\255"}, {"\127"}}, "packet too large")

local oldOpen, oldRename, oldRemove = io.open, os.rename, os.remove
io.open = function(path, mode)
  if mode == "w" then return {write = function(_, value) files[path] = value end, close = function() end} end
  if not files[path] then return nil end
  return {read = function() return files[path] end, close = function() end}
end
os.rename = function(from, to)
  if not files[from] then return nil end
  files[to], files[from] = files[from], nil
  return true
end
os.remove = function(path) files[path] = nil; return true end
local worker = coroutine.create(tasks["harmony-local-resources"])
files["/data/resources/DeviceList.json"] = "{}"
files["/data/resources/ProtocolList.json"] = "{}"
files[flag] = "DeviceList\nProtocolList\n"
assert(coroutine.resume(worker))
assert(broadcasts == 1 and not files[flag] and not files[flag .. ".loading"])
-- Reloads continue even when the MQTT task never connects or runs.
files[flag] = "DeviceList\n"
assert(coroutine.resume(worker))
assert(broadcasts == 2 and not files[flag])
files[flag] = "DeviceList\n"
files["/data/codex/local/transaction/pending"] = "1"
assert(coroutine.resume(worker))
assert(broadcasts == 2 and files[flag])
files["/data/codex/local/transaction/pending"] = nil
assert(coroutine.resume(worker))
assert(broadcasts == 3 and not files[flag])
io.open, os.rename, os.remove = oldOpen, oldRename, oldRemove

local payloads, topics = {}, {}
package.loaded["tasks.harmonywebservices.core.session"].getAccount = function() return {} end
sys.getNetworkAttribute = function() return "192.0.2.1" end
sys.getFirmwareVersion = function() return "test" end
package.loaded.json.encode = function(payload)
  if payload.unique_id then payloads[payload.unique_id] = payload end
  return "{}"
end
upvalue(connection, "publishDiscovery")({send = function(_, packet)
  assert(packet:byte(1) == 49) -- Retained PUBLISH.
  topics[#topics+1] = packet
  return #packet
end}, {haDiscovery=true, clientId="test", baseTopic="hub", discoveryPrefix="ha", name="Hub"}, {devices={}})
local sensors = {
  activity_id={"Activity ID", "activityId", "mdi:identifier"},
  ip={"IP Address", "ip", "mdi:ip-network"},
  firmware={"Firmware", "firmware", "mdi:chip"},
  ir_devices={"IR Devices", "deviceCount", "mdi:remote"},
  ir_commands={"IR Commands", "commandCount", "mdi:counter"}
}
for id, expected in pairs(sensors) do
  local p = assert(payloads["test_" .. id])
  assert(p.name == expected[1] and p.icon == expected[3])
  assert(p.value_template == "{{ value_json." .. expected[2] .. " }}")
  assert(p.state_topic == "hub/state" and p.availability_topic == "hub/status")
  assert(p.entity_category == "diagnostic" and p.device.identifiers[1] == "test")
  local found = false
  for _, packet in ipairs(topics) do
    if packet:find("ha/sensor/test_" .. id .. "/config", 1, true) then found = true end
  end
  assert(found)
end
print("MQTT disconnect, independent resource reload, and discovery checks passed")

-- Sequence switching skips shared-device power toggles but still sends inputs.
local requested = {}
local rawDecode, rawEncode = package.loaded.json.decode, package.loaded.json.encode
package.loaded.json.decode = function(value) return value end
package.loaded.json.encode = function(value) return value end
io.open = function(path, mode)
  if mode == "w" then return {write = function(_, value) files[path] = value end, close = function() end} end
  if files[path] == nil then return nil end
  return {read = function() return files[path] end, close = function() end}
end
core.command = function(command)
  requested[#requested + 1] = command
  local id = "step-" .. #requested
  files["/tmp/harmony-operations/" .. id .. ".json"] = {state = "completed"}
  return {id = id}
end
local previous = {steps = {{deviceId = "tv", role = "power-on"}}, exitSteps = {{deviceId = "tv", command = "Off"}, {deviceId = "amp", command = "Off"}}}
local op = {id = "activity-test", request = {steps = {{deviceId = "tv", command = "On", role = "power-on"}, {deviceId = "tv", command = "HDMI1"}}, exitSteps = {}}}
assert(core.runActivity(op, previous))
assert(#requested == 2 and requested[1].deviceId == "amp" and requested[2].command == "HDMI1")
op.request.fixState = true; requested = {}
assert(core.runActivity(op, previous))
assert(#requested == 2 and requested[1].command == "On")
files["/tmp/harmony-operations/activity-test.cancel"] = "1"
local ok, err = core.runActivity(op, previous)
assert(not ok and err == "cancelled")
-- Exercise the actual task: pcall cannot yield around a sequence on Lua 5.1.
os.rename, os.remove = function(from, to)
  files[to], files[from] = files[from], nil; return true
end, function(path) files[path] = nil; return true end
local root = "/tmp/harmony-operations/"
local sequence = {id = "delayed", kind = "activity", state = "queued",
  request = {id = "local-test", steps = {{kind = "delay", delayMs = 200}, {deviceId = "tv", command = "On"}}}}
files[root .. "activity-current"], files[root .. "delayed.json"] = sequence.id, sequence
local activityWorker = coroutine.create(tasks["harmony-local-activities"])
local resumed, wait = coroutine.resume(activityWorker)
assert(resumed and wait == 100 and sequence.state == "running")
assert(coroutine.resume(activityWorker))
assert(coroutine.resume(activityWorker))
assert(sequence.state == "completed" and files[root .. "activity-state.json"].activityId == "local-test")
local interrupted = {id = "cancel-delay", kind = "activity", state = "queued",
  request = {id = "cancel-test", steps = {{kind = "delay", delayMs = 200}}}}
files[root .. "activity-current"], files[root .. "cancel-delay.json"] = interrupted.id, interrupted
assert(coroutine.resume(activityWorker))
assert(interrupted.state == "running")
files[root .. "cancel-delay.cancel"] = "1"
assert(coroutine.resume(activityWorker))
assert(interrupted.state == "cancelled" and files[root .. "activity-state.json"].activityId == "")
io.open, os.rename, os.remove = oldOpen, oldRename, oldRemove
package.loaded.json.decode, package.loaded.json.encode = rawDecode, rawEncode
print("Local activity switching, estimated-state repair and cancellation passed")
