local skynet = require "skynet"
local sc = require "skynet.socketchannel"
local socket = require "skynet.socket"
local cluster = require "skynet.cluster.core"

local config_name = skynet.getenv "cluster"
local node_address = {}
local node_session = {}
local command = {}

local function read_response(sock)
	local sz = socket.header(sock:read(2))
	local msg = sock:read(sz)
	return cluster.unpackresponse(msg)	-- session, ok, data, padding
end

local connecting = {}

local function open_channel(t, key)
	local ct = connecting[key]
	if ct then
		local co = coroutine.running()
		table.insert(ct, co)
		skynet.wait(co)
		return assert(ct.channel)
	end
	ct = {}
	connecting[key] = ct
	local address = node_address[key]
	if address == nil then
		local co = coroutine.running()
		assert(ct.namequery == nil)
		ct.namequery = co
		skynet.error("Wating for cluster node [".. key.."]")
		skynet.wait(co)
		address = node_address[key]
		assert(address ~= nil)
	end
	local succ, err, c
	if address then
		local host, port = string.match(address, "([^:]+):(.*)$")
		c = sc.channel {
			host = host,
			port = tonumber(port),
			response = read_response,
			nodelay = true,
		}
		succ, err = pcall(c.connect, c, true)
		if succ then
			t[key] = c
			ct.channel = c
		end
	else
		err = "cluster node [" .. key .. "] is down."
	end
	connecting[key] = nil
	for _, co in ipairs(ct) do
		skynet.wakeup(co)
	end
	assert(succ, err)
	return c
end

local node_channel = setmetatable({}, { __index = open_channel })

local function loadconfig(tmp)
	if tmp == nil then
		tmp = {}
		if config_name then
			local f = assert(io.open(config_name))
			local source = f:read "*a"
			f:close()
			assert(load(source, "@"..config_name, "t", tmp))()
		end
	end
	for name,address in pairs(tmp) do
		assert(address == false or type(address) == "string")
		if node_address[name] ~= address then
			-- address changed
			if rawget(node_channel, name) then
				node_channel[name] = nil	-- reset connection
			end
			node_address[name] = address
		end
		local ct = connecting[name]
		if ct and ct.namequery then
			skynet.error(string.format("Cluster node [%s] resloved : %s", name, address))
			skynet.wakeup(ct.namequery)
		end
	end
end

function command.reload(source, config)
	loadconfig(config)
	skynet.ret(skynet.pack(nil))
end

function command.listen(source, addr, port)
	local gate = skynet.newservice("gate")
	if port == nil then
		local address = assert(node_address[addr], addr .. " is down")
		addr, port = string.match(address, "([^:]+):(.*)$")
	end
	skynet.call(gate, "lua", "open", { address = addr, port = port })
	skynet.ret(skynet.pack(nil))
end

local function send_request(source, node, addr, msg, sz)
	local session = node_session[node] or 1
	-- msg is a local pointer, cluster.packrequest will free it
	local request, new_session, padding = cluster.packrequest(addr, session, msg, sz)
	node_session[node] = new_session

	-- node_channel[node] may yield or throw error
	local c = node_channel[node]

	return c:request(request, session, padding)
end

function command.req(...)
	local ok, msg = pcall(send_request, ...)
	if ok then
		if type(msg) == "table" then
			skynet.ret(cluster.concat(msg))
		else
			skynet.ret(msg)
		end
	else
		skynet.error(msg)
		skynet.response()(false)
	end
end

function command.push(source, node, addr, msg, sz)
	local session = node_session[node] or 1
	local request, new_session, padding = cluster.packpush(addr, session, msg, sz)
	if padding then	-- is multi push
		node_session[node] = new_session
	end

	-- node_channel[node] may yield or throw error
	local c = node_channel[node]

	c:request(request, nil, padding)

	-- notice: push may fail where the channel is disconnected or broken.
end

local proxy = {}

function command.proxy(source, node, name)
	if name == nil then
		node, name = node:match "^([^@.]+)([@.].+)"
		if name == nil then
			error ("Invalid name " .. tostring(node))
		end
	end
	local fullname = node .. "." .. name
	if proxy[fullname] == nil then
		proxy[fullname] = skynet.newservice("clusterproxy", node, name)
	end
	skynet.ret(skynet.pack(proxy[fullname]))
end

local cluster_agent = {}	-- fd:service
local register_name = {}

local function clearnamecache()
	for fd, service in pairs(cluster_agent) do
		if type(service) == "number" then
			skynet.send(service, "lua", "namechange")
		end
	end
end

function command.register(source, name, addr)
	assert(register_name[name] == nil)
	addr = addr or source
	local old_name = register_name[addr]
	if old_name then
		register_name[old_name] = nil
		clearnamecache()
	end
	register_name[addr] = name
	register_name[name] = addr
	skynet.ret(nil)
	skynet.error(string.format("Register [%s] :%08x", name, addr))
end

function command.queryname(source, name)
	skynet.ret(skynet.pack(register_name[name]))
end

function command.socket(source, subcmd, fd, msg)
	if subcmd == "open" then
		skynet.error(string.format("socket accept from %s", msg))
		-- new cluster agent
		cluster_agent[fd] = false
		local agent = skynet.newservice("clusteragent", skynet.self(), source, fd)
		local closed = cluster_agent[fd]
		cluster_agent[fd] = agent
		if closed then
			skynet.send(agent, "lua", "exit")
			cluster_agent[fd] = nil
		end
	else
		if subcmd == "close" or subcmd == "error" then
			-- close cluster agent
			local agent = cluster_agent[fd]
			if type(agent) == "boolean" then
				cluster_agent[fd] = true
			else
				skynet.send(agent, "lua", "exit")
				cluster_agent[fd] = nil
			end
		else
			skynet.error(string.format("socket %s %d %s", subcmd, fd, msg or ""))
		end
	end
end

skynet.start(function()
	loadconfig()
	skynet.dispatch("lua", function(session , source, cmd, ...)
		local f = assert(command[cmd])
		f(source, ...)
	end)
end)
