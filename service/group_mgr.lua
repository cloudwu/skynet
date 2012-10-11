local skynet = require "skynet"

-- read group_local for id 10000
local id = 10000
local harbor_ctrl = {}
local multicast = {}
local command = {}

function command.MASTER(address, harbor)
	harbor_ctrl[harbor] = address
end

local function create_group_in(harbor, id)
	local local_ctrl = assert(harbor_ctrl[harbor])
	local g = multicast[id]
	g[harbor] = false
	local local_mc = skynet.call(local_ctrl, "lua", "CREATE", id)
	for _,mc in pairs(g) do
		if mc then
			skynet.send(local_ctrl, "lua", "AGENT", id, mc)
		end
	end
	for harbor_id,ctrl in pairs(harbor_ctrl) do
		if harbor_id ~= harbor then
			skynet.send(ctrl, "lua", "AGENT", id, local_mc)
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

	skynet.send(local_ctrl, "lua", "ENTER", id, address)
end

function command.LEAVE(address, harbor, id)
	assert(multicast[id], id)
	local local_ctrl = assert(harbor_ctrl[harbor])
	skynet.send(local_ctrl, "lua", "LEAVE", id, address)
end

function command.DELETE(_,_, id)
	assert(multicast[id], id)
	multicast[id] = nil

	for harbor_id,_ in pairs(g) do
		skynet.send(harbor_ctrl[harbor_id], "lua", "CLEAR", id)
	end
end

skynet.start(function()
	skynet.dispatch("lua", function(_, _, cmd, address, param)
		local f = command[cmd]
		assert(f, cmd, param)
		local harbor = skynet.harbor(address)
		f(address, harbor, param)
	end)
end)

