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

local function open_channel(t, key)
	local host, port = string.match(node_address[key], "([^:]+):(.*)$")
	local c = sc.channel {
		host = host,
		port = tonumber(port),
		response = read_response,
		nodelay = true,
	}
	assert(c:connect(true))
	t[key] = c
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
		assert(type(address) == "string")
		if node_address[name] ~= address then
			-- address changed
			if rawget(node_channel, name) then
				node_channel[name] = nil	-- reset connection
			end
			node_address[name] = address
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
		addr, port = string.match(node_address[addr], "([^:]+):(.*)$")
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
	local ok, msg, sz = pcall(send_request, ...)
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
	local fullname = node .. "." .. name
	if proxy[fullname] == nil then
		proxy[fullname] = skynet.newservice("clusterproxy", node, name)
	end
	skynet.ret(skynet.pack(proxy[fullname]))
end

local register_name = {}

function command.register(source, name, addr)
	assert(register_name[name] == nil)
	addr = addr or source
	local old_name = register_name[addr]
	if old_name then
		register_name[old_name] = nil
	end
	register_name[addr] = name
	register_name[name] = addr
	skynet.ret(nil)
	skynet.error(string.format("Register [%s] :%08x", name, addr))
end

local large_request = {}

function command.socket(source, subcmd, fd, msg)
	if subcmd == "data" then
		local sz
		local addr, session, msg, padding, is_push = cluster.unpackrequest(msg)
		if padding then
			local req = large_request[session] or { addr = addr , is_push = is_push }
			large_request[session] = req
			table.insert(req, msg)
			return
		else
			local req = large_request[session]
			if req then
				large_request[session] = nil
				table.insert(req, msg)
				msg,sz = cluster.concat(req)
				addr = req.addr
				is_push = req.is_push
			end
			if not msg then
				local response = cluster.packresponse(session, false, "Invalid large req")
				socket.write(fd, response)
				return
			end
		end
		local ok, response
		if addr == 0 then
			local name = skynet.unpack(msg, sz)
			local addr = register_name[name]
			if addr then
				ok = true
				msg, sz = skynet.pack(addr)
			else
				ok = false
				msg = "name not found"
			end
		elseif is_push then
			skynet.rawsend(addr, "lua", msg, sz)
			return	-- no response
		else
			ok , msg, sz = pcall(skynet.rawcall, addr, "lua", msg, sz)
		end
		if ok then
			response = cluster.packresponse(session, true, msg, sz)
			if type(response) == "table" then
				for _, v in ipairs(response) do
					socket.lwrite(fd, v)
				end
			else
				socket.write(fd, response)
			end
		else
			response = cluster.packresponse(session, false, msg)
			socket.write(fd, response)
		end
	elseif subcmd == "open" then
		skynet.error(string.format("socket accept from %s", msg))
		skynet.call(source, "lua", "accept", fd)
	else
		large_request = {}
		skynet.error(string.format("socket %s %d : %s", subcmd, fd, msg))
	end
end

skynet.start(function()
	loadconfig()
	skynet.dispatch("lua", function(session , source, cmd, ...)
		local f = assert(command[cmd])
		f(source, ...)
	end)
end)
