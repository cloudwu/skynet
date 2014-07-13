local skynet = require "skynet"

skynet.start(function()
	skynet.newservice "logind"
end)
