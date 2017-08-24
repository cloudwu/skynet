local skynet = require "skynet"

local list = {}

local function timeout_check(ti)
	if not next(list) then
		return
	end
	skynet.sleep(ti)	-- sleep 10 sec
	for k,v in pairs(list) do
		skynet.error("timout",ti,k,v)
	end
end

skynet.start(function()
	skynet.error("ping all")
	local list_ret = skynet.call(".launcher", "lua", "LIST")
	for addr, desc in pairs(list_ret) do
		list[addr] = desc
		skynet.fork(function()
			skynet.call(addr,"debug","INFO")
			list[addr] = nil
		end)
	end
	skynet.sleep(0)
	timeout_check(100)
	timeout_check(400)
	timeout_check(500)
	skynet.exit()
end)
