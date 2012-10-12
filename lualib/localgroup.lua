local skynet = require "skynet"

local lg

local group = {}

function group.create()
	return skynet.call(lg, "lua", "NEW" , skynet.self())
end

group.address = assert(skynet.query_group)
group.enter = assert(skynet.enter_group)
group.leave = assert(skynet.leave_group)

function group.release(id)
	skynet.send(lg, "lua" , "DELETE", id)
end

skynet.init(function()
	lg = skynet.uniqueservice("group_local")
	assert(lg)
end, "localgroup")

return group
