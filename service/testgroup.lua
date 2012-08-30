local skynet = require "skynet"

skynet.dispatch()

skynet.start(function()
	local group = skynet.query_group(1)
	for i=1,10 do
		local address = skynet.newservice("testgroup_c", tostring(i))
		skynet.enter_group(1 , address)
	end
	skynet.send(group,"Hello World")
	skynet.exit()
end)
