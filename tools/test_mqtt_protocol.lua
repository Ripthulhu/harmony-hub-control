-- Protocol/discovery regression tests with no network and no device.
local json = require("cjson")
local tasks, files, calls = {}, {}, {}
package.loaded.json = json
package.loaded.system = {
  safeCall = pcall, addTask = function(name, fn) tasks[name] = fn; return name end,
  sleep = function() coroutine.yield() end,
  getNetworkAttribute = function() return "192.0.2.1" end,
  getFirmwareVersion = function() return "4.15.600" end, getHostName = function() return "Hub" end,
}
package.loaded["tasks.codex.localcore"] = {start = function() end,
  command = function(c) calls[#calls+1] = c; return {id="test-"..#calls, state="queued"} end,
  request = function(path, body) calls[#calls+1] = {path=path, body=body}; return {id="test-"..#calls} end}
local realOpen = io.open
io.open = function(path) return files[path] and {read=function() return files[path] end, close=function() end} end
assert(loadfile("payload/mqtt/codexmqtt.lua"))("protocol_test")
protocol_test.discover()
local function up(fn, name, replacement)
  for i=1,60 do local k,v=debug.getupvalue(fn,i); if k==name then if replacement ~= nil then debug.setupvalue(fn,i,replacement) end; return v end end
  error("missing "..name)
end
local connection = up(tasks.codexmqtt, "runConnection")
local protect = up(tasks.codexmqtt, "protected")
local send = up(connection, "sendAll")
local function run(fn)
  local co=coroutine.create(fn)
  for _=1,200 do assert(coroutine.resume(co)); if coroutine.status(co)=="dead" then return end end
  error("test did not finish")
end
local offsets={}
run(function()
  local ok,value=protect(function() coroutine.yield("scheduler",5); return 42 end)
  assert(ok and value==42, "protected session must yield on stock Lua 5.1")
  assert(not protect(function() error("broken session") end))
end)
run(function()
  assert(send({send=function(_,raw,pos)
    offsets[#offsets+1]=pos
    if #offsets==1 then return nil,"timeout",3 end
    return #raw
  end}, "abcdefgh"))
end)
assert(offsets[1]==1 and offsets[2]==4)
run(function() local ok,err=send({send=function(_,raw,pos) return nil,"timeout",pos-1 end},"x"); assert(not ok and err=="MQTT write timed out") end)
up(send,"writeError",false)
local packets={}
local sock={send=function(_,raw) packets[#packets+1]=raw; return #raw end}
local cfg={clientId="existing-hub",name="Hub",baseTopic="hub",discoveryPrefix="ha",haDiscovery=true}
local handle=up(connection,"handlePublish")
local function message(topic,payload,flags)
  return {body=string.char(0,#topic)..topic..payload,flags=flags or 0}
end
local command=json.encode({deviceId="12",command="VolumeUp"})
assert(handle(sock,cfg,message("hub/command",command),true))
assert(handle(sock,cfg,message("hub/command",command),true))
assert(#calls==2, "rapid identical taps must both be sent")
assert(handle(sock,cfg,message("hub/command",command,1),true))
assert(#calls==2, "retained commands must never execute")
assert(handle(sock,cfg,message("hub/command",command),false))
assert(#calls==2, "offline control is rejected")
assert(not handle(sock,cfg,{body="\0\50short",flags=0},true))
assert(not handle(sock,cfg,message("hub/command",command,2),true))
assert(handle(sock,cfg,message("hub/hbus",'{"cmd":"anything"}'),true))
assert(handle(sock,cfg,message("hub/command",'{"deviceId":"12","command":"VolumeUp","mode":"hold"}'),true))
assert(#calls==2)
files["/data/codex/local/config.json"]=json.encode({activities={{id="a",name="TV"},{id="b",name="TV"}}})
assert(handle(sock,cfg,message("hub/activity/set","TV"),true))
assert(#calls==2, "duplicate activity names must be rejected")
assert(handle(sock,cfg,message("hub/activity/set","TV (b)"),true))
assert(calls[3].body.activityId=="b")
local publishDiscovery=up(connection,"publishDiscovery")
local inventory={devices={{id="12",name="TV",manufacturer="LG",model="C5",commands={{name="VolumeUp"},{name="A+B"},{name="A B"}}}},deviceCount=1,totalCommandCount=3}
local desired=publishDiscovery(sock,cfg,inventory,{},true)
local count=0
for topic,raw in pairs(desired) do
  count=count+1; local p=json.decode(raw)
  assert(p.availability_topic=="hub/status")
  if p.name=="VolumeUp" then assert(p.enabled_by_default and p.device.via_device=="existing-hub") end
  if p.name=="A+B" then assert(not p.enabled_by_default) end
end
assert(count==10, "all command IDs must be collision-free")
if arg[1] then
  local f=assert(realOpen(arg[1],"w")); f:write(json.encode(desired)); f:close()
end
packets={}
publishDiscovery(sock,cfg,inventory,desired,false)
assert(#packets==0, "unchanged discovery must not be republished")
packets={}
publishDiscovery(sock,cfg,{devices={}},desired,false)
assert(#packets==3, "deleted commands must clear their retained discovery")
local state=up(connection,"statePayload")
local unknown=json.decode(state(cfg,inventory))
assert(unknown.activity=="" and unknown.activityId=="", "boot is not evidence of powered-off devices")
files["/tmp/harmony-operations/activity-state.json"]='{"activityId":"power-off"}'
assert(json.decode(state(cfg,inventory)).activity=="PowerOff")
local pending=up(up(handle,"result"),"pending")
files["/tmp/harmony-operations/test-1.json"]='{"state":"completed","result":{"ok":true}}'
local completions=up(connection,"publishOperations")
completions({send=function() return nil,"closed",0 end},cfg)
assert(pending["test-1"], "failed writes must keep terminal results for reconnect")
up(send,"writeError",false)
completions(sock,cfg)
assert(not pending["test-1"])
local realExecute=os.execute
os.execute=function() return 1 end
packets={}
assert(not publishDiscovery(sock,cfg,inventory,{},true,"fixture-owner"))
assert(#packets==0, "low storage must prevent untracked discovery publications")
up(send,"writeError",false)
os.execute=realExecute
io.open=realOpen
print("MQTT: partial writes, bounded timeout, repeated taps, retained controls, framing, discovery identity/pruning, and unknown state passed")
