local skynet = require "skynet"

skynet.dispatch()

local command ="*2\r\n$3\r\nGET\r\n$1\r\nA\r\n"

skynet.start(function()
	local cli = skynet.call(".launcher","broker redis snlua redis-cli.lua 127.0.0.1:6379")
	print("redis-cli:", cli)
	assert(cli)
	local result = skynet.call(cli, command)
	print(result)
	skynet.exit()
end)

