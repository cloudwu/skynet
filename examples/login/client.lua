package.cpath = "luaclib/?.so"

--[[ Status code

200 OK

400 Bad Request	(通常是登陆协议错误)
401 Unauthorized (通常是登陆服务器或游戏服务器验证错误)
403 Forbidden	(通常是连接游戏服务器的 index 已经过期)
404 Not Found   (通常是游戏服务器未获得登陆服务器的通知)
406 Not Acceptable (通常是登陆服务器转发游戏服务器拒绝登陆)
412 Precondition Failed	(通常是遗漏了和游戏服务器前次通讯的请求)

]]

local socket = require "clientsocket"
local cjson = require "cjson"
local crypt = require "crypt"

local last
local fd = assert(socket.connect("127.0.0.1", 8001))
local input = {}

local function readline()
	local line = table.remove(input, 1)
	if line then
		return line
	end

	while true do
		local status
		status, last = socket.readline(fd, last, input)
		if status == nil then
			error "Server closed"
		end
		if not status then
			socket.usleep(100)
		else
			local line = table.remove(input, 1)
			if line then
				return line
			end
		end
	end
end

local challenge = crypt.base64decode(readline())

local clientkey = crypt.randomkey()
socket.writeline(fd, crypt.base64encode(crypt.dhexchange(clientkey)))
local secret = crypt.dhsecret(crypt.base64decode(readline()), clientkey)

print("sceret is ", crypt.hexencode(secret))

local hmac = crypt.hmac64(challenge, secret)
socket.writeline(fd, crypt.base64encode(hmac))

local token = {
	server = "sample",
	user = "hello",
	pass = "password",
}

local etoken = crypt.desencode(secret, cjson.encode(token))
local b = crypt.base64encode(etoken)
socket.writeline(fd, crypt.base64encode(etoken))

print(readline())

socket.close(fd)

----- connect to game server

local input = {}
local fd = assert(socket.connect("127.0.0.1", 8888))

local function readpackage()
	local line = table.remove(input, 1)
	if line then
		return line
	end

	while true do
		local status
		status, last = socket.recv(fd, last, input)
		if status == nil then
			error "Server closed"
		end
		if not status then
			socket.usleep(100)
		else
			local line = table.remove(input, 1)
			if line then
				return line
			end
		end
	end
end

local index = 0
local request = 0
local handshake = string.format("%s@%s:%d:%d", crypt.base64encode(token.user), crypt.base64encode(token.server) , index, request)
local hmac = crypt.hmac64(crypt.hashkey(handshake), secret)

socket.send(fd, handshake .. ":" .. crypt.base64encode(hmac))

print(readpackage())

socket.send(fd , "echo")
print(readpackage())






