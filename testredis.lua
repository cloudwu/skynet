local skynet = require "skynet"

skynet.dispatch()

local command ="*2\r\n$3\r\nGET\r\n$1\r\nA\r\n"

skynet.start(function()
	local cli = skynet.call(".launcher","broker redis snlua redis-cli.lua 127.0.0.1:7379")
	print("redis-cli:", cli)
	assert(cli)
	print(skynet.call(cli, skynet.unpack, skynet.pack("GET","A")))
	skynet.exit()
end)

