local skynet = require "skynet"
local stm = require "skynet.stm"

local mode = ...

if mode == "slave" then

skynet.start(function()
	skynet.dispatch("lua", function (_,_, obj)
		local obj = stm.newcopy(obj)
		print("read:", obj(skynet.unpack))
		skynet.ret()
		skynet.error("sleep and read")
		for i=1,10 do
			skynet.sleep(10)
			print("read:", obj(skynet.unpack))
		end
		skynet.exit()
	end)
end)

else

skynet.start(function()
	local slave = skynet.newservice(SERVICE_NAME, "slave")
	local obj = stm.new(skynet.pack(1,2,3,4,5))
	local copy = stm.copy(obj)
	skynet.call(slave, "lua", copy)
	for i=1,5 do
		skynet.sleep(20)
		print("write", i)
		obj(skynet.pack("hello world", i))
	end
 	skynet.exit()
end)
end
