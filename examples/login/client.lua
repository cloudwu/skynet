package.cpath = "luaclib/?.so"

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
	user = "hello",
	pass = "password",
}

local etoken = crypt.desencode(secret, cjson.encode(token))
local b = crypt.base64encode(etoken)
socket.writeline(fd, crypt.base64encode(etoken))

print(readline())





