local skynet = require "skynet"
local gateserver = require "gamefw.gateserver"
local netpack = require "netpack"
local crypt = require "crypt"
local socketdriver = require "socketdriver"
local datacenter = require "datacenter"

local users = {}

skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
}

local handler = {}
local handshake = {}
local connection = {}
local agent = {}
local login_master

-- launch an agent service to handle message from client
local function launch_agent(c)
	local agent = assert(skynet.newservice "msgagent")
	skynet.call(agent, "lua", "init", c.uid, c.server)
	return agent
end

function handler.connect(fd, addr)
	skynet.error(string.format("new connection from %s (fd=%d)", addr, fd))
	handshake[fd] = true
	gateserver.openclient(fd)
end

local function auth(fd, msg, sz)
--	base64(uid)@base64(server)#base64(subid):index:request:base64(hmac)
	local message = netpack.tostring(msg, sz)
	local username, index, request , hmac = string.match(message, "([^:]*):([^:]*):([^:]*):([^:]*)")
	local content = users[username]
	if content == nil then
		return "404 Not Found"
	end
	local idx = assert(tonumber(index))
	local req = assert(tonumber(request))
	hmac = crypt.base64decode(hmac)

	if idx < content.index then
		return "403 Forbidden"
	end
	if req > content.request or req < content.reserved then
		return "412 Precondition Failed"
	end

	local text = string.format("%s:%s:%s", username, index, request)
	local v = crypt.hmac64(crypt.hashkey(text), content.secret)
	if v ~= hmac then
		return "401 Unauthorized"
	end

	content.index = idx
	for i=content.reserved, request do
		content.response[i] = nil
	end
	content.reserved = request
	content.reqest = request
	connection[fd] = content

	if content.fd then
		local last_fd = content.fd
		connection[last_fd] = nil
		gateserver.closeclient(last_fd)
	end
	content.fd = fd

	if content.agent == nil then
		content.agent = true
		local ok, agent = pcall(launch_agent, content)
		if ok then
			content.agent = agent
		else
			content.agent = nil
			skynet.error(string.format("Launch agent %s failed : %s", content.uid, agent))
			connection[fd] = nil
			gateserver.closeclient(fd)
		end
	end
end

function handler.message(fd, msg, sz)
	if handshake[fd] then
		local ok, result = pcall(auth, fd, msg, sz)
		if not ok then
			result = "400 Bad Request"
		end

		local close = result ~= nil

		if result == nil then
			result = "200 OK"
		end

		socketdriver.send(fd, netpack.pack(result))

		if close then
			gateserver.closeclient(fd)
		end
		handshake[fd] = nil
	else
		local c = connection[fd]
		if c == nil or c.agent == nil then
			local message = netpack.tostring(msg, sz)
			skynet.error(string.format("Unknown fd = %d, message (%s) size = %d", fd, crypt.hexencode(message):sub(1,80), #message))
			return
		end
		c.request = c.request + 1
		local ret = c.response[c.request]
		if ret == nil then
			local ok, msg, sz = pcall(skynet.rawcall, c.agent, "client", msg, sz)
			if ok then
				ret = netpack.pack_string(msg, sz)
			else
				skynet.error(string.format("%s request error : %s", c.username, msg))
				ret = netpack.pack_string ""
			end
			c.response[c.request] = ret
		end
		socketdriver.send(c.fd, ret)
	end
end

function handler.close(fd)
	handshake[fd] = nil
	local c = connection[fd]
	if c then
		c.fd = nil
	end
end

handler.error = handler.close

function handler.open(source, conf)
	login_master = assert(conf.loginserver)
	local servername = assert(conf.servername)
	skynet.call(login_master, "lua", "register_gate", servername, skynet.self())
end


local CMD = {}

function CMD.login(source, server, uid, secret)
	local subid = "1"
	local username = crypt.base64encode(uid) .. '#'..crypt.base64encode(subid)..'@' .. crypt.base64encode(server)
	users[username] = {
		server = server,
		uid = uid,
		secret = secret,
		index = 0,
		request = 0,
		reserved = 0,
		response = {},
	}
	return subid
end

function CMD.logout(source)
	local c = agent[source]
	if c then
		skynet.call(login_master, "lua", "logout", c.uid)
		if c.fd then
			gateserver.closeclient(c.fd)
		end
	end
end

function CMD.kick(source, server, uid)
	local username = crypt.base64encode(uid) .. '@' .. crypt.base64encode(server)
	local u = users[username]
	if u and u.agent then
		skynet.call(u.agent, "logout")
		skynet.kill(u.agent)
		u.agent = nil
	end
end

function handler.command(cmd, source, ...)
	local f = assert(CMD[cmd])
	return f(source, ...)
end

gateserver.start(handler)
