local skynet = require "skynet"

local clusterd
local cluster = {}
local sender = {}
local inquery_name = {}

local function get_sender(t, node)
	local waitco = inquery_name[node]
	if waitco then
		local co=coroutine.running()
		table.insert(waitco, co)
		skynet.wait(co)
		return rawget(t, node)
	else
		waitco = {}
		inquery_name[node] = waitco
		local c = skynet.call(clusterd, "lua", "sender", node)
		inquery_name[node] = nil
		t[node] = c
		for _, co in ipairs(waitco) do
			skynet.wakeup(co)
		end
		return c
	end
end

setmetatable(sender, { __index = get_sender } )

function cluster.call(node, address, ...)
	-- skynet.pack(...) will free by cluster.core.packrequest
	return skynet.call(sender[node], "lua", "req",  address, skynet.pack(...))
end

function cluster.send(node, address, ...)
	-- push is the same with req, but no response
	skynet.send(sender[node], "lua", "push", address, skynet.pack(...))
end

function cluster.open(port)
	if type(port) == "string" then
		skynet.call(clusterd, "lua", "listen", port)
	else
		skynet.call(clusterd, "lua", "listen", "0.0.0.0", port)
	end
end

function cluster.reload(config)
	skynet.call(clusterd, "lua", "reload", config)
end

function cluster.proxy(node, name)
	return skynet.call(clusterd, "lua", "proxy", node, name)
end

function cluster.snax(node, name, address)
	local snax = require "skynet.snax"
	if not address then
		address = cluster.call(node, ".service", "QUERY", "snaxd" , name)
	end
	local handle = skynet.call(clusterd, "lua", "proxy", node, address)
	return snax.bind(handle, name)
end

function cluster.register(name, addr)
	assert(type(name) == "string")
	assert(addr == nil or type(addr) == "number")
	return skynet.call(clusterd, "lua", "register", name, addr)
end

function cluster.query(node, name)
	return skynet.call(sender[node], "lua", "req", 0, skynet.pack(name))
end

skynet.init(function()
	clusterd = skynet.uniqueservice("clusterd")
end)

return cluster
