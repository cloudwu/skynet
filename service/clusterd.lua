local skynet = require "skynet"
local sc = require "socketchannel"
local socket = require "socket"
local cluster = require "cluster.core"

local config_name = skynet.getenv "cluster"
local node_address = {}
local node_session = {}
local command = {}

local function read_response(sock)
	local sz = socket.header(sock:read(2))
	local msg = sock:read(sz)
	return cluster.unpackresponse(msg)	-- session, ok, data
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

local function loadconfig()
	local f = assert(io.open(config_name))
	local source = f:read "*a"
	f:close()
	local tmp = {}
	assert(load(source, "@"..config_name, "t", tmp))()
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

function command.reload()
	loadconfig()
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
	local request, new_session = cluster.packrequest(addr, session, msg, sz)
	local c = node_channel[node]
	node_session[node] = new_session

	return c:request(request, session)
end

function command.req(...)
	local ok, msg, sz = pcall(send_request, ...)
	if ok then
		skynet.ret(msg, sz)
	else
		skynet.error(msg)
		skynet.response()(false)
	end
end

local proxy = {}

function command.proxy(source, node, name)
	local fullname = node .. "." .. name
	if proxy[fullname] == nil then
		proxy[fullname] = skynet.newservice("clusterproxy", node, name)
	end
	skynet.ret(skynet.pack(proxy[fullname]))
end

local request_fd = {}

function command.socket(source, subcmd, fd, msg)
	if subcmd == "data" then
		local addr, session, msg = cluster.unpackrequest(msg)
		local ok , msg, sz = pcall(skynet.rawcall, addr, "lua", msg)
		local response
		if ok then
			response = cluster.packresponse(session, true, msg, sz)
		else
			response = cluster.packresponse(session, false, msg)
		end
		socket.write(fd, response)
	elseif subcmd == "open" then
		skynet.error(string.format("socket accept from %s", msg))
		skynet.call(source, "lua", "accept", fd)
	else
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
