local skynet = require "skynet"

local SERVICE

local group = {}

function group.create()
	return skynet.call(SERVICE, "lua", "NEW" , skynet.self())
end

function group.address(id)
	local send_id = id * 2 + 1
	return skynet.query_group(send_id)
end

function group.enter(id, handle)
	handle = handle or skynet.self()
	skynet.send(SERVICE, "lua" , "ENTER", handle, id)
end

function group.leave(id, handle)
	handle = handle or skynet.self()
	skynet.send(SERVICE, "lua" , "LEAVE", handle, id)
end

function group.release(id)
	skynet.send(SERVICE, "lua" , "DELETE", skynet.self(), id)
end

skynet.init(function()
	SERVICE = skynet.uniqueservice(true, "group_mgr")
	skynet.uniqueservice("group_agent", SERVICE)
end, "mcgroup")

return group
