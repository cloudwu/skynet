local skynet = require "skynet"
require "skynet.manager"
local socket = require "socket"
local crypt = require "crypt"
local table = table
local string = string
local assert = assert

--[[

Protocol:

	line (\n) based text protocol

	1. Server->Client : base64(8bytes random challenge)
	2. Client->Server : base64(8bytes handshake client key)
	3. Server: Gen a 8bytes handshake server key
	4. Server->Client : base64(DH-Exchange(server key))
	5. Server/Client secret := DH-Secret(client key/server key)
	6. Client->Server : base64(HMAC(challenge, secret))
	7. Client->Server : DES(secret, base64(token))
	8. Server : call auth_handler(token) -> server, uid (A user defined method)
	9. Server : call login_handler(server, uid, secret) ->subid (A user defined method)
	10. Server->Client : 200 base64(subid)

Error Code:
	400 Bad Request . challenge failed
	401 Unauthorized . unauthorized by auth_handler
	403 Forbidden . login_handler failed
	406 Not Acceptable . already in login (disallow multi login)

Success:
	200 base64(subid)
]]

local socket_error = {}
local function assert_socket(service, v, fd)
	if v then
		return v
	else
		skynet.error(string.format("%s failed: socket (fd = %d) closed", service, fd))
		error(socket_error)
	end
end

local function write(service, fd, text)
	assert_socket(service, socket.write(fd, text), fd)
end

local function launch_slave(auth_handler)
	local function auth(fd, addr)
		-- set socket buffer limit (8K)
		-- If the attacker send large package, close the socket
		socket.limit(fd, 8192)

		local challenge = crypt.randomkey()
		write("auth", fd, crypt.base64encode(challenge).."\n")

		local handshake = assert_socket("auth", socket.readline(fd), fd)
		local clientkey = crypt.base64decode(handshake)
		if #clientkey ~= 8 then
			error "Invalid client key"
		end
		local serverkey = crypt.randomkey()
		write("auth", fd, crypt.base64encode(crypt.dhexchange(serverkey)).."\n")

		local secret = crypt.dhsecret(clientkey, serverkey)

		local response = assert_socket("auth", socket.readline(fd), fd)
		local hmac = crypt.hmac64(challenge, secret)

		if hmac ~= crypt.base64decode(response) then
			write("auth", fd, "400 Bad Request\n")
			error "challenge failed"
		end

		local etoken = assert_socket("auth", socket.readline(fd),fd)

		local token = crypt.desdecode(secret, crypt.base64decode(etoken))

		local ok, server, uid =  pcall(auth_handler,token)

		return ok, server, uid, secret
	end

	local function ret_pack(ok, err, ...)
		if ok then
			return skynet.pack(err, ...)
		else
			if err == socket_error then
				return skynet.pack(nil, "socket error")
			else
				return skynet.pack(false, err)
			end
		end
	end

	local function auth_fd(fd, addr)
		skynet.error(string.format("connect from %s (fd = %d)", addr, fd))
		socket.start(fd)	-- may raise error here
		local msg, len = ret_pack(pcall(auth, fd, addr))
		socket.abandon(fd)	-- never raise error here
		return msg, len
	end

	skynet.dispatch("lua", function(_,_,...)
		local ok, msg, len = pcall(auth_fd, ...)
		if ok then
			skynet.ret(msg,len)
		else
			skynet.ret(skynet.pack(false, msg))
		end
	end)
end

local user_login = {}

local function accept(conf, s, fd, addr)
	-- call slave auth
	local ok, server, uid, secret = skynet.call(s, "lua",  fd, addr)
	-- slave will accept(start) fd, so we can write to fd later

	if not ok then
		if ok ~= nil then
			write("response 401", fd, "401 Unauthorized\n")
		end
		error(server)
	end

	if not conf.multilogin then
		if user_login[uid] then
			write("response 406", fd, "406 Not Acceptable\n")
			error(string.format("User %s is already login", uid))
		end

		user_login[uid] = true
	end

	local ok, err = pcall(conf.login_handler, server, uid, secret)
	-- unlock login
	user_login[uid] = nil

	if ok then
		err = err or ""
		write("response 200",fd,  "200 "..crypt.base64encode(err).."\n")
	else
		write("response 403",fd,  "403 Forbidden\n")
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
		skynet.ret(skynet.pack(conf.command_handler(command, ...)))
	end)

	for i=1,instance do
		table.insert(slave, skynet.newservice(SERVICE_NAME))
	end

	skynet.error(string.format("login server listen at : %s %d", host, port))
	local id = socket.listen(host, port)
	socket.start(id , function(fd, addr)
		local s = slave[balance]
		balance = balance + 1
		if balance > #slave then
			balance = 1
		end
		local ok, err = pcall(accept, conf, s, fd, addr)
		if not ok then
			if err ~= socket_error then
				skynet.error(string.format("invalid client (fd = %d) error = %s", fd, err))
			end
		end
		socket.close_fd(fd)	-- We haven't call socket.start, so use socket.close_fd rather than socket.close.
	end)
end

local function login(conf)
	local name = "." .. (conf.name or "login")
	skynet.start(function()
		local loginmaster = skynet.localname(name)
		if loginmaster then
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
