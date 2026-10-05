local system = require("system")
local json = require("json")
local socket = require("socket")
local M = {}
local started = false
local ROOT = "/tmp/harmony-operations/"
local RELOAD = "/data/codex/reload_resources"

-- Activity waits must yield through the stock Lua 5.1 scheduler.
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
  local s = f:read("*a"); f:close(); return s
end
local function decode(path)
  local s = read(path)
  if not s then return nil end
  local ok, v = pcall(json.decode, s)
  return ok and type(v) == "table" and v or nil
end
local function write(path, value)
  local raw = type(value) == "table" and json.encode(value) or value
  local f = io.open(path .. ".lua-new", "w")
  if not f then return nil, "Cannot save runtime state" end
  f:write(raw); f:close()
  return os.rename(path .. ".lua-new", path)
end
local function id(s)
  return type(s) == "string" and #s <= 96 and s:match("^[%w_.-]+$")
end

-- Yield while C sends through the stock engine in this same Lua scheduler.
function M.request(path, body)
  local key = read("/data/codex/local/internal.key")
  if not key then return nil, "Local integration key is missing" end
  key = key:gsub("%s+$", "")
  local sock, err = socket.tcp()
  if not sock then return nil, err end
  sock:settimeout(1)
  local ok; ok, err = sock:connect("127.0.0.1", 8080)
  if not ok then sock:close(); return nil, err end
  local raw = body and json.encode(body) or ""
  local request = (body and "POST" or "GET") .. " " .. path .. " HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nAuthorization: Bearer " .. key ..
    "\r\nX-Harmony-CSRF: " .. key .. "\r\nContent-Type: application/json\r\nContent-Length: " .. #raw .. "\r\nConnection: close\r\n\r\n" .. raw
  ok, err = sock:send(request)
  if not ok then sock:close(); return nil, err end
  sock:settimeout(0)
  local chunks, size = {}, 0
  for _ = 1, 400 do
    local chunk, why, partial = sock:receive(4096)
    chunk = chunk or partial
    if chunk and #chunk > 0 then chunks[#chunks + 1] = chunk; size = size + #chunk end
    if size > 65536 then sock:close(); return nil, "API response too large" end
    if why == "closed" then
      sock:close()
      local text = table.concat(chunks):match("\r\n\r\n(.*)")
      local parsed, result = pcall(json.decode, text or "")
      if not parsed then return nil, "Invalid API response" end
      if result.ok == false then return nil, result.error or "Control failed" end
      return result
    end
    system.sleep(25)
  end
  sock:close(); return nil, "Local API timed out"
end

function M.command(command)
  if type(command) ~= "table" then return nil, "Invalid command" end
  if command.status and command.status ~= "pressrelease" then return nil, "Use paired controller hold ownership" end
  return M.request("/api/v1/commands/send", {
    deviceId = tostring(command.deviceId or ""), command = tostring(command.command or ""),
    transport = command.transport or "ir", mode = "tap", parentId = command.parentId
  })
end

-- Opt-in development path. Never replaces an existing household or its identity.
function M.bootstrap()
  if not read("/data/codex/local/bootstrap.requested") then return true end
  local devices = decode("/data/resources/DeviceList.json")
  if devices and devices.DevicesWithFeatures and next(devices.DevicesWithFeatures) then
    return nil, "Blank initialization refused: existing devices must be kept or restored"
  end
  local manager = require("tasks.harmonywebservices.core.resourcemanager")
  local empty = {
    DeviceList = '{"DevicesWithFeatures":[]}', ProtocolList = '{"Protocols":[]}',
    ActivityList = '{"Activities":[]}', MapList = '{"ButtonMaps":[],"FunctionMaps":null}',
    FunctionList = '{"FunctionMaps":[]}', AutomationConfig = '{"devices":{}}'
  }
  for name, raw in pairs(empty) do
    if not read("/data/resources/" .. name .. ".json") then
      local ok, err = manager.saveResource("application/json", "local-1", raw, name)
      if not ok or read("/data/resources/" .. name .. ".json") ~= raw then
        return nil, tostring(err or "Native resource creation could not be verified")
      end
    end
  end
  -- Context is optional to the engine's local account; no fabricated account,
  -- cloud token, manufacturing preference or remote ID is written here.
  os.remove("/data/codex/local/bootstrap.requested")
  write(RELOAD, "all")
  return true
end

function M.reload()
  if read("/data/codex/local/transaction/pending") then return end
  local loading = RELOAD .. ".loading"
  if read(loading) then return end
  local requested = read(RELOAD)
  if not requested then return end
  if not os.rename(RELOAD, loading) then return end
  local allowed = {DeviceList = true, FunctionList = true, ProtocolList = true, ActivityList = true, MapList = true, AutomationConfig = true}
  local ok, resources = true, {}
  if requested:match("^%s*all%s*$") then requested = "DeviceList,ProtocolList,ActivityList,MapList,AutomationConfig" end
  for resource in requested:gmatch("[^,%s]+") do
    if not allowed[resource] then ok = false; break end
    local value = decode("/data/resources/" .. resource .. ".json")
    if not value then ok = false; break end
    resources[#resources + 1] = resource
  end
  if ok and #resources > 0 then ok = pcall(system.broadcastMessage, "config_new", resources) end
  os.remove(loading)
  if not ok then write(ROOT .. "resource-error", "A native resource is invalid. Restore a known backup.")
  else os.remove(ROOT .. "resource-error") end
end

local function cancelled(op)
  return read(ROOT .. op.id .. ".cancel") ~= nil
end
local function delay(ms, op)
  while ms > 0 do
    if cancelled(op) then return nil, "cancelled" end
    local step = math.min(ms, 100); system.sleep(step); ms = ms - step
  end
  return true
end
local function step(action, op)
  if cancelled(op) then return nil, "cancelled" end
  if action.kind ~= "delay" then
    local queued, err = M.command({deviceId = action.deviceId, command = action.command, transport = action.transport, parentId = op.id})
    if not queued then return nil, err end
    for _ = 1, 500 do
      local result = decode(ROOT .. queued.id .. ".json")
      if cancelled(op) then M.request("/api/v1/operations", {id = queued.id, action = "cancel"}); return nil, "cancelled" end
      if result and result.state == "completed" then break end
      if result and (result.state == "failed" or result.state == "cancelled") then return nil, result.result and result.result.error or result.state end
      if _ == 500 then return nil, "Command timed out" end
      system.sleep(50)
    end
  end
  return delay(tonumber(action.delayMs) or 0, op)
end
function M.runActivity(op, previous)
  local activity = op.request
  if type(activity) ~= "table" or type(activity.steps) ~= "table" then return nil, "Invalid activity" end
  if activity.stopAll then
    for _, action in ipairs(previous and previous.exitSteps or {}) do
      local ok, err = step(action, op); if not ok then return nil, err end
    end
    return true
  end
  local shared = {}
  for _, action in ipairs(activity.steps) do if action.deviceId then shared[action.deviceId] = true end end
  if previous and not activity.fixState then
    for _, action in ipairs(previous.exitSteps or {}) do
      if not shared[action.deviceId] then local ok, err = step(action, op); if not ok then return nil, err end end
    end
  end
  local prior = {}
  if previous and not activity.fixState then for _, action in ipairs(previous.steps or {}) do if action.deviceId then prior[action.deviceId] = true end end end
  for _, action in ipairs(activity.steps) do
    if not (action.role == "power-on" and prior[action.deviceId]) then
      local ok, err = step(action, op); if not ok then return nil, err end
    end
  end
  return true
end
local function activities()
  local previous = nil
  write(ROOT .. "activity-state.json", {activityId = "", estimated = true})
  local current = read(ROOT .. "activity-current")
  if id(current) then
    local interrupted = decode(ROOT .. current .. ".json")
    if interrupted and interrupted.kind == "activity" and interrupted.state == "running" then
      interrupted.state = "failed"; interrupted.result = {ok = false, error = "Activity service restarted; device state is unknown"}
      write(ROOT .. current .. ".json", interrupted)
    end
  end
  while true do
    write(ROOT .. "activities-ready", "1")
    local current = read(ROOT .. "activity-current")
    if id(current) then
      local op = decode(ROOT .. current .. ".json")
      if op and op.kind == "activity" and op.state == "queued" then
        op.state = "running"; write(ROOT .. current .. ".json", op)
        local called, ok, err = protected(M.runActivity, op, previous)
        if not called then err = tostring(ok); ok = nil end
        op.state = ok and "completed" or err == "cancelled" and "cancelled" or "failed"
        op.result = {ok = ok and true or false, error = err, stateEstimated = true}
        write(ROOT .. current .. ".json", op)
        if ok and not op.request.stopAll then previous = op.request
        else previous = nil end
        write(ROOT .. "activity-state.json", {activityId = ok and op.request.id or "", estimated = true})
      end
    end
    system.sleep(100)
  end
end
function M.start()
  if started then return true end
  started = true
  os.remove(RELOAD .. ".loading")
  system.addTask("harmony-local-resources", function()
    local ok, err = M.bootstrap()
    if not ok then write(ROOT .. "resource-error", err) end
    while true do
      local ok = pcall(M.reload)
      if not ok then os.remove(RELOAD .. ".loading"); write(ROOT .. "resource-error", "Native resource reload failed") end
      local session = require("tasks.harmonywebservices.core.session")
      if session.getAccount() then write(ROOT .. "core-ready", "1")
      else os.remove(ROOT .. "core-ready") end
      system.sleep(250)
    end
  end)
  system.addTask("harmony-local-activities", activities)
  return true
end
return M
