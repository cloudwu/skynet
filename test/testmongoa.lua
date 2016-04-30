local skynet = require "skynet"
local mongo = require "mongo"

skynet.start(function()
	local db = mongo.client({host = "127.0.0.1", port=3717})
	skynet.error(db:auth_scram_sha1("game", "q123456"))
	skynet.exit()
end)
