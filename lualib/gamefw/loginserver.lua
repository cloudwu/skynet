local skynet = require "skynet"
local socket = require "socket"
local crypt = require "crypt"

local function launch_slave(auth_handler)
	local cmd = {}

	-- set socket buffer limit (8K)
	-- If the attacker send large package, close the socket
	socket.limit(8192)

	function cmd.auth(fd, addr)
		skynet.error(string.format("connect from %s (fd = %d)", addr, fd))
		socket.start(fd)
		local challenge = crypt.randomkey()
		socket.write(fd, crypt.base64encode(challenge).."\n")

		local handshake = assert(socket.readline(fd), "socket closed")
		local clientkey = crypt.base64decode(handshake)
		if #clientkey ~= 8 then
			error "Invalid client key"
		end
		local serverkey = crypt.randomkey()
		socket.write(fd, crypt.base64encode(crypt.dhexchange(serverkey)).."\n")

		local secret = crypt.dhsecret(clientkey, serverkey)

		local response = assert(socket.readline(fd), "socket closed")
		local hmac = crypt.hmac64(challenge, secret)

		if hmac ~= crypt.base64decode(response) then
			socket.write(fd, "400 Bad Request\n")
			error "challenge failed"
		end

		local etoken = assert(socket.readline(fd), "socket closed")

		local token = crypt.desdecode(secret, crypt.base64decode(etoken))

		local ok, server, uid =  pcall(auth_handler,token)

		socket.abandon(fd)
		return ok, server, uid, secret
	end

	skynet.dispatch("lua", function(_,_,command,...)
		local f = assert(cmd[command])
		skynet.ret(skynet.pack(f(...)))
	end)
end

local function accept(conf, s, fd, addr)
	local ok, server, uid, secret = skynet.call(s, "lua", "auth", fd, addr)
	if not ok then
		socket.write(fd, "401 Unauthorized\n")
		error(server)
	end

	socket.start(fd)

	local ok, err = pcall(conf.login_handler, server, uid, secret)
	if ok then
		socket.write(fd,  "200 OK\n")
	else
		socket.write(fd,  "406 Not Acceptable\n")
		error(err)
	end
end

local function launch_master(conf)
	local instance = conf.instance or 8
	assert(instance > 0)
	local host = conf.host or "0.0.0.0"
	local port = assert(tonumber(conf.port))
	local slave = {}
	local balance = 1

	skynet.dispatch("lua", function(_,source,command, ...)
		if command == "register_slave" then
			table.insert(slave, source)
			skynet.ret(skynet.pack(nil))
		else
			skynet.ret(skynet.pack(conf.command_handler(command, source, ...)))
		end
	end)

	for i=1,instance do
		skynet.newservice(SERVICE_NAME)
	end

	local id = socket.listen(host, port)
	skynet.error(string.format("login server listen at : %s %d", host, port))
	socket.start(id , function(fd, addr)
		local s = slave[balance]
		balance = balance + 1
		if balance > #slave then
			balance = 1
		end
		local ok, err = pcall(accept, conf, s, fd, addr)
		if not ok then
			skynet.error(string.format("invalid client (fd = %d) error = %s", fd, err))
		end
		socket.close(fd)
	end)
end

local function login (conf)
	local name = "." .. (conf.name or "login")
	skynet.start(function()
		local loginmaster = skynet.localname(name)
		if loginmaster then
			skynet.call(loginmaster, "lua", "register_slave")
			local auth_handler = assert(conf.auth_handler)
			launch_master = nil
			conf = nil
			launch_slave(auth_handler)
		else
			launch_slave = nil
			conf.auth_handler = nil
			assert(conf.login_handler)
			assert(conf.command_handler)
			skynet.register(name)
			launch_master(conf)
		end
	end)
end

return login
