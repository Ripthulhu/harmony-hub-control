local hbus = require("tasks.hal.core.hbus"):instance()
local log = require("log").logger("cs.netservicestarter")
local mfgData = require("tasks.mfg.core.mfgdata")
local prefMgr = require("tasks.harmonywebservices.core.preferencemanager")
local session = require("tasks.harmonywebservices.core.session")
local system = require("system")

MSG_NETSERVICE_NEW_ADDRESS = "cs.netservicestarter_new_address"

local ipaddr
local discovery
local ssdpDiscovery
local halConnect
local hbusHttpServerConnect
local ltcpServerConnector
local cloudapi
local pubnubServerConnect
local packagemgr

local function cloudBlockerEnabled()
  if not io or not io.open then
    return true
  end
  local f = io.open("/data/codex/cloud_blocker.conf", "r")
  if not f then
    return true
  end
  local value = string.lower(tostring(f:read("*l") or ""))
  f:close()
  return not (value == "0" or value == "off" or value == "false" or value == "disabled" or value == "allow" or value == "allowed")
end

local function startCloudModule(label, moduleName)
  log.notice("starting " .. label .. " task")
  local ok, task = pcall(require, moduleName)
  if not ok then
    log.notice("codex cloud task load failed", label, tostring(task))
    return nil
  end
  if task and task.start then
    local started, err = pcall(function()
      task.start()
    end)
    if not started then
      log.notice("codex cloud task start failed", label, tostring(err))
    end
  end
  return task
end

local function maybeStartCloudTasks()
  if cloudBlockerEnabled() then
    log.notice("codex cloud blocker active: cloudapi, pubnub, and packagemgr background tasks not started")
    return
  end
  if not cloudapi then
    cloudapi = startCloudModule("cloudapi", "tasks.connectserver.transport.cloudapi")
  end
  if not pubnubServerConnect then
    pubnubServerConnect = startCloudModule("PubNub Connection", "tasks.connectserver.transport.pubnubwrapper")
  end
  if not packagemgr or (packagemgr.taskStatus and packagemgr.taskStatus() == "dead") then
    packagemgr = startCloudModule("packagemgr", "tasks.connectserver.transport.packagemgr")
  end
end

local function logWifiEvent(name, id)
  if cloudBlockerEnabled() then return end
  local usageLog = require("tasks.crashlog.apihandler.usagelog")
  local t = {
    category = "hub connectivity",
    name = name
  }
  if id then
    t.id = id
  end
  usageLog.postAnalyticEvent(t)
end

local function handleEvent(event)
  if event.family ~= "inet" or event.label == "br-lan" then
    return
  end

  if event.msgtype == "newaddr" then
    log.notice(event.label .. ":newaddr", event.address)
    if event.label ~= "lo" then
      session.setIp(event.address)
    end

    local uuid = system.uniqueId()
    if not cloudBlockerEnabled() then
      local usageLog = require("tasks.crashlog.apihandler.usagelog")
      usageLog.setUniqueId("wifi", uuid)
    end
    logWifiEvent("connect wifi", uuid)

    if ipaddr ~= event.address and event.label ~= "lo" then
      ipaddr = event.address
    end

    if not discovery then
      log.notice("discovery: starting task")
      discovery = system.loadTask("tasks/connectserver/transport/discovery.lua")
    end
    if not ssdpDiscovery and event.label ~= "lo" then
      log.notice("ssdpDiscovery: starting task")
      ssdpDiscovery = system.loadTask("tasks/connectserver/transport/ssdpdiscovery.lua")
    end
    if not halConnect then
      log.notice("starting HAL task")
      halConnect = system.loadTask("tasks/connectserver/transport/halhttpserverconnector.lua")
    end
    if not hbusHttpServerConnect then
      log.notice("starting HBUS over HTTP server task")
      hbusHttpServerConnect = system.loadTask("tasks/connectserver/transport/hbushttpserverconnector.lua")
    end
    if not ltcpServerConnector then
      log.notice("starting LTCP Server task")
      ltcpServerConnector = system.loadTask("tasks/connectserver/transport/ltcpserverconnector.lua")
    end
    require("tasks.codex.localcore").start()

    if event.label ~= "lo" then
      maybeStartCloudTasks()
      system.broadcastMessageExceptMeNoWarning(MSG_NETSERVICE_NEW_ADDRESS, {
        label = event.label,
        ipaddr = event.address
      })
    end
  elseif event.msgtype == "deladdr" then
    log.notice(event.label .. ":deladdr", event.address)
    if event.label ~= "lo" then
      session.setIp(nil)
      if not cloudBlockerEnabled() then
        local usageLog = require("tasks.crashlog.apihandler.usagelog")
        logWifiEvent("disconnect wifi", usageLog.getUniqueId("wifi"))
      end
    end
  end
end

if mfgData.hasNetwork == true then
  require("tasks.codex.localcore").start()
  local waited = 0
  while not system.isMessageRegistered("config_unload") or not system.isMessageRegistered("get_setup_account") do
    if waited >= 600 then
      log.notice("local engine readiness timed out; starting network services in degraded mode")
      break
    end
    system.sleep(100); waited = waited + 1
  end
  local netlink = system.netlinkOpen()
  local current = system.getNetworkAttribute("ipaddr")
  if current and current ~= "0.0.0.0" then handleEvent({family = "inet", label = "ath0", msgtype = "newaddr", address = current}) end
  while true do
    system.yieldSocketRecv(netlink)
    local events = netlink:receive()
    if events then
      local processEvent = false
      local newEvent
      for i, event in ipairs(events) do
        log.notice("netlink event", i, event)
        if event.msgtype == "newaddr" then
          newEvent = event
        end
        if event.family == "inet" and event.label ~= "lo" then
          handleEvent(event)
          processEvent = true
          break
        end
      end
      if newEvent and not processEvent then
        handleEvent(newEvent)
      end
    end
  end
  netlink:close()
else
  require("tasks.codex.localcore").start()
  log.notice("starting HAL task")
  halConnect = system.loadTask("tasks/connectserver/transport/halhttpserverconnector.lua")
  log.notice("starting HBUS over HTTP server task")
  hbusHttpServerConnect = system.loadTask("tasks/connectserver/transport/hbushttpserverconnector.lua")
  log.notice("starting LTCP Server task")
  ltcpServerConnector = system.loadTask("tasks/connectserver/transport/ltcpserverconnector.lua")
end
