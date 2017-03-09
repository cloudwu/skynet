local skynet = require "skynet"
local sprotoloader = require "sprotoloader"
local snax = require "snax"

local max_client = 64

skynet.start(function()
	skynet.error("Server start")
	skynet.uniqueservice("protoloader")
	if not skynet.getenv "daemon" then
		local console = skynet.newservice("console")
	end
	skynet.newservice("debug_console",8000)
	skynet.newservice("simpledb")
	local watchdog = skynet.newservice("watchdog")
	skynet.call(watchdog, "lua", "start", {
		port = 8888,
		maxclient = max_client,

		nodelay = true,
	})
	skynet.error("Watchdog listen on", 8888)

	local testservice = snax.uniqueservice("testservice")

	local myservice1 = skynet.newservice("myservice1")
	print(skynet.call(myservice1, "lua", 1, 2, 3))

	local myservice2 = skynet.newservice("myservice2")
	print(skynet.call(myservice2, "lua", 1, 2, 3))


	local echo_reload = skynet.newservice("echo_reload")

	skynet.exit()
end)
