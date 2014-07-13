local login = require "gamefw.loginserver"
local crypt = require "crypt"
local skynet = require "skynet"

local server = {
	host = "127.0.0.1",
	port = 8001,
	name = "login_master",
}

local server_list = {}
local user_online = {}

local server_mt = {}
server_mt.__index = server_mt

function server_mt:kick(uid)
	skynet.call(self.address, "lua", "kick", self.name, uid)
end

function server_mt:login(uid, secret)
	skynet.call(self.address, "lua", "login", self.name, uid, secret)
end

function server.auth_handler(token)
	-- the token is base64(user)@base64(server):base64(password)
	local user, server, password = token:match("([^@]+)@([^:]+):(.+)")
	user = crypt.base64decode(user)
	server = crypt.base64decode(server)
	password = crypt.base64decode(password)
	assert(password == "password")
	return server, user
end

function server.login_handler(server, uid, secret)
	print(string.format("%s@%s is login, secret is %s", uid, server, crypt.hexencode(secret)))
	local u = user_online[uid]
	if u then
		u:kick(uid)
	end
	assert(user_online[uid] == nil, "kick failed")
	local gameserver = assert(server_list[server], "Unknown server")
	gameserver:login(uid, secret)
	user_online[uid] = gameserver
end

local CMD = {}

function CMD.register_gate(server, address)
	server_list[server] = setmetatable( { name = server, address = address }, server_mt )
end

function CMD.logout(uid)
	local u = user_online[uid]
	if u then
		print(string.format("%s@%s is logout", uid, u.name))
		user_online[uid] = nil
	end
end

function server.command_handler(command, source, ...)
	local f = assert(CMD[command])
	return f(source, ...)
end

login(server)
