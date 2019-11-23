local skynet = require "skynet"
local socket = require "skynet.socket"

skynet.start(function()
	skynet.newservice("server")
	skynet.fork(function()
		local id, err = socket.open("127.0.0.1", 8001)
		assert(not err)

		skynet.sleep(100)

		for i = 1, 10 do
			local s = socket.read(id)
			print("read", s)
			socket.write(id, tostring(os.time()))
			skynet.sleep(50)
		end

		socket.close(id)
		skynet.exit()
	end)
end)
