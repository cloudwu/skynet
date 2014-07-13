local login = require "gamefw.loginserver"
local json = require "cjson"
local crypt = require "crypt"
local skynet = require "skynet"

local server = {
	host = "127.0.0.1",
	port = 8001,
	name = "login_master",
}

local server_list = {}
local user_online = {}

function server.auth_handler(token)
	token = json.decode(token)
	assert(token.user)
	assert(token.pass == "password")
	return token.server, token.user
end

function server.login_handler(server, uid, secret)
	print(string.format("%s@%s is login, secret is %s", uid, server, crypt.hexencode(secret)))
	local u = user_online[uid]
	if u then
		local gameserver = server_list[u.server]
		skynet.call(gameserver, "lua", "kick", server, uid)
	end
	local gameserver = assert(server_list[server])
	skynet.call(gameserver, "lua", "login", server, uid, secret)
end

local CMD = {}

function CMD.register_gate(source, name)
	server_list[name] = source
end

function CMD.logout(source, uid, server)
	print(string.format("%s@%s is logout", uid, server))
end

function server.command_handler(command, source, ...)
	local f = assert(CMD[command])
	return f(source, ...)
end

login(server)
