-- Disposable test process. Every hub file and command is redirected into a temp directory.
local root, port = assert(arg[1]), tonumber(assert(arg[2]))
local socket, json = require("socket"), require("cjson")
local open, rename, execute = io.open, os.rename, os.execute
local function path(p) return root .. "/" .. p:gsub("/", "_") end
io.open = function(p, mode) return open(p:sub(1,1)=="/" and path(p) or p, mode) end
os.rename = function(a,b) return rename(path(a),path(b)) end
os.execute = function(cmd) assert(cmd:find("codex_webui --space",1,true)); return 0 end
local function write(p,v) local f=assert(io.open(p,"w")); f:write(json.encode(v)); f:close() end
write("/data/codexmqtt/config.json",{enabled=true,broker={host="127.0.0.1",port=port},baseTopic="test/hub",discoveryPrefix="ha",clientId="fixture",keepAlive=10,pollSeconds=2})
write("/data/codex/local/config.json",{activities={}})
local task, count
count=0
package.loaded.json=json
package.loaded.system={safeCall=pcall,sleep=function(ms) socket.sleep(ms/1000) end,
  addTask=function(name,fn) task=fn; return name end,
  getNetworkAttribute=function() return "192.0.2.1" end,getFirmwareVersion=function() return "test" end,getHostName=function() return "Test hub" end}
package.loaded["tasks.codex.localcore"]={start=function() end,
  request=function(p)
    assert(p=="/api/v1/devices")
    local f=assert(open(root.."/inventory.json")); local v=json.decode(f:read("*a")); f:close(); return v
  end,
  command=function(c)
    count=count+1
    local id="fixture-"..count
    write("/tmp/harmony-operations/"..id..".json",{id=id,state="completed",result={ok=true}})
    local f=assert(open(root.."/commands.jsonl","a")); f:write(json.encode(c),"\n"); f:close()
    return {id=id,state="queued"}
  end}
assert(loadfile("payload/mqtt/codexmqtt.lua"))("fixture")
fixture.discover()
task()
