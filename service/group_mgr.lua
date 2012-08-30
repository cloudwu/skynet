local skynet = require "skynet"

local id = 0
local harbor_ctrl = {}
local multicast = {}
local command = {}

function command.MASTER(address, harbor)
	harbor_ctrl[harbor] = address
end

local function create_group_in(harbor, id)
	local local_ctrl = assert(harbor_ctrl[harbor])
	local g = multicast[id]
	local local_mc = skynet.call(local_ctrl, skynet.unpack, skynet.pack("CREATE", id))
	for _,mc in pairs(g) do
		skynet.send(local_ctrl, 0, skynet.pack("AGENT", id, mc))
	end
	for harbor_id,ctrl in pairs(harbor_ctrl) do
		if harbor_id ~= harbor then
			skynet.send(ctrl,0, skynet.pack("AGENT", id, local_mc))
		end
	end
	g[harbor] = local_mc
end

function command.NEW()
	id = id + 1
	multicast[id] = {}
	skynet.ret(skynet.pack(id))
end

function command.ENTER(address, harbor, id)
	local g = multicast[id]
	assert(g,id)
	if g[harbor] == nil then
		create_group_in(harbor, id)
	end
	local local_ctrl = assert(harbor_ctrl[harbor])

	skynet.send(local_ctrl, 0, skynet.pack("ENTER", id, address))
end

function command.LEAVE(address, harbor, id)
	local g = multicast[id]
	assert(g,id)
	local local_ctrl = assert(harbor_ctrl[harbor])
	skynet.send(local_ctrl, 0, skynet.pack("LEAVE", id, address))
end

function command.DELETE(_,_, id)
	local g = multicast[id]
	assert(g,id)
	multicast[id] = nil

	for harbor_id,_ in pairs(g) do
		skynet.send(harbor_ctrl[harbor_id],0, skynet.pack("CLEAR", id))
	end
end

skynet.dispatch(function (msg,sz)
	local cmd , address , param = skynet.unpack(msg,sz)
	local f = command[cmd]
	assert(f, cmd, param)
	local harbor = skynet.harbor(address)
	f(address, harbor, param)
end)

skynet.register("GROUPMGR")