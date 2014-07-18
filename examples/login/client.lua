package.cpath = "luaclib/?.so"

local socket = require "clientsocket"
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

local function encode_token(token)
	return string.format("%s@%s:%s",
		crypt.base64encode(token.user),
		crypt.base64encode(token.server),
		crypt.base64encode(token.pass))
end

local etoken = crypt.desencode(secret, encode_token(token))
local b = crypt.base64encode(etoken)
socket.writeline(fd, crypt.base64encode(etoken))

local result = readline()
print(result)
local code = tonumber(string.sub(result, 1, 3))
assert(code == 200)
socket.close(fd)

local subid = crypt.base64decode(string.sub(result, 5))

print("login ok, subid=", subid)

----- connect to game server

local function send_request(v, session)
	local s =  string.char(bit32.extract(session,24,8), bit32.extract(session,16,8), bit32.extract(session,8,8), bit32.extract(session,0,8))
	socket.send(fd , v..s)
	return v, session
end

local function recv_response(v)
	local content = v:sub(1,-6)
	local ok = v:sub(-5,-5):byte()
	local session = 0
	for i=-4,-1 do
		local c = v:byte(i)
		session = session + bit32.lshift(c,(-1-i) * 8)
	end
	return ok ~=0 , content, session
end

local input = {}

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

local text = "echo"
local index = 1

print("connect")
local fd = assert(socket.connect("127.0.0.1", 8888))
input = {}

local handshake = string.format("%s@%s#%s:%d", crypt.base64encode(token.user), crypt.base64encode(token.server),crypt.base64encode(subid) , index)
local hmac = crypt.hmac64(crypt.hashkey(handshake), secret)

socket.send(fd, handshake .. ":" .. crypt.base64encode(hmac))

print(readpackage())
print("===>",send_request(text,0))
-- don't recv response
-- print("<===",recv_response(readpackage()))

print("disconnect")
socket.close(fd)

index = index + 1

print("connect again")
local fd = assert(socket.connect("127.0.0.1", 8888))
input = {}

local handshake = string.format("%s@%s#%s:%d", crypt.base64encode(token.user), crypt.base64encode(token.server),crypt.base64encode(subid) , index)
local hmac = crypt.hmac64(crypt.hashkey(handshake), secret)

socket.send(fd, handshake .. ":" .. crypt.base64encode(hmac))

print(readpackage())
print("===>",send_request("fake",0))	-- request again (use last session 0, so the request message is fake)
print("===>",send_request("again",1))	-- request again (use new session)
print("<===",recv_response(readpackage()))
print("<===",recv_response(readpackage()))


print("disconnect")
socket.close(fd)

