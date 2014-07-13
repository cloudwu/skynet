local skynet = require "skynet"

skynet.start(function()
	local loginserver = skynet.newservice "logind"
	local gate = skynet.newservice "msggate"
	skynet.call(gate, "lua", "open" , {
		port = 8888,
		maxclient = 64,
		loginserver = loginserver,
		servername = "sample",
	})
end)
