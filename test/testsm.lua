local skynet = require "skynet"
local sharemap = require "skynet.sharemap"

local mode = ...

if mode == "slave" then
--slave

local function dump(reader)
	reader:update()
	print("x=", reader.x)
	print("y=", reader.y)
	print("s=", reader.s)
end

skynet.start(function()
	local reader
	skynet.dispatch("lua", function(_,_,cmd,...)
		if cmd == "init" then
			reader = sharemap.reader(...)
		else
			assert(cmd == "ping")
			dump(reader)
		end
		skynet.ret()
	end)
end)

else
-- master
skynet.start(function()
	-- register share type schema
	sharemap.register("./test/sharemap.sp")
	local slave = skynet.newservice(SERVICE_NAME, "slave")
	local writer = sharemap.writer("foobar", { x=0,y=0,s="hello" })
	skynet.call(slave, "lua", "init", "foobar", writer:copy())
	writer.x = 1
	writer:commit()
	skynet.call(slave, "lua", "ping")
	writer.y = 2
	writer:commit()
	skynet.call(slave, "lua", "ping")
	writer.s = "world"
	writer:commit()
	skynet.call(slave, "lua", "ping")
end)

end