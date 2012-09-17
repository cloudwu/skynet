local skynet = require "skynet"

local group_mgr = ...

group_mgr = tonumber(group_mgr)

local command = {}
local group = {}

function command.CREATE(id)
	assert(group[id] == nil)
	group[id] = {}
	local recv_id = id * 2
	local addr = skynet.query_group(recv_id)
	skynet.ret(skynet.pack(addr))
end

function command.AGENT(id, handle)
	local tunnel = skynet.launch("tunnel", skynet.address(handle))
	assert(tunnel)
	local send_id = id * 2 + 1
	skynet.enter_group(send_id, tunnel)
	table.insert(group[id] , tunnel)
end

function command.ENTER(id, handle)
	local recv_id = id * 2
	local send_id = id * 2 + 1
	skynet.enter_group(recv_id, handle)
	skynet.enter_group(send_id, handle)
end

function command.LEAVE(id, handle)
	local recv_id = id * 2
	local send_id = id * 2 + 1
	skynet.leave_group(recv_id, handle)
	skynet.leave_group(send_id, handle)
end

function command.CLEAR(id)
	local g = group[id]
	assert(g)
	local recv_id = id * 2
	local send_id = id * 2 + 1
	skynet.clear_group(recv_id)
	skynet.clear_group(send_id)
	group[id] = nil
	for _,v in ipairs(g) do
		skynet.kill(v)
	end
end

skynet.start(function()
	skynet.dispatch("lua", function(session, address, cmd , id , handle)
		local f = command[cmd]
		assert(f, cmd)
		f(id,handle)
	end)
	skynet.send(group_mgr , "lua", "MASTER", skynet.self())
end)
