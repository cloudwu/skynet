local login = require "loginserver"
local json = require "cjson"
local crypt = require "crypt"

local server = {
	host = "127.0.0.1",
	port = 8001,
	name = "login_master",
}

function server.auth_handler(token)
	token = json.decode(token)
	assert(token.user)
	assert(token.pass == "password")
	return "sample", token.user
end

function server.login_handler(server, uid, secret)
	print(string.format("%s@%s is login, secret is %s", uid, server, crypt.hexencode(secret)))
end

function server.logout_handler(server, uid)
	print(string.format("%s@%s is logout", uid, server))
end

login(server)
