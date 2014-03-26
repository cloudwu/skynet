local skynet = require "skynet"
local group = require "mcgroup"
--local group = require "localgroup"

skynet.start(function()
	local gid = group.create()
	local gaddr = group.address(gid)
	local g = {}
	print("=== Create Group ===",gid,skynet.address(gaddr))
	for i=1,10 do
		local address = skynet.newservice("testgroup_c", tostring(i))
		table.insert(g, address)
		group.enter(gid , address)
	end
	skynet.cast(g,"text","Cast")
	skynet.sleep(1000)
	skynet.send(gaddr,"text","Hello World")
	skynet.exit()
end)
