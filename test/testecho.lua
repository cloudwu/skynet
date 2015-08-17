local skynet = require "skynet"

local mode = ...

if mode == "slave" then

skynet.start(function()
	skynet.dispatch("lua", function(_,_, ...)
		skynet.ret(skynet.pack(...))
	end)
end)

else

skynet.start(function()
	local slave = skynet.newservice(SERVICE_NAME, "slave")
	local n = 100000
	local start = skynet.now()
	print("call salve", n, "times in queue")
	for i=1,n do
		skynet.call(slave, "lua")
	end
	print("qps = ", n/ (skynet.now() - start) * 100)

	start = skynet.now()

	local worker = 10
	local task = n/worker
	print("call salve", n, "times in parallel, worker = ", worker)

	for i=1,worker do
		skynet.fork(function()
			for i=1,task do
				skynet.call(slave, "lua")
			end
			worker = worker -1
			if worker == 0 then
				print("qps = ", n/ (skynet.now() - start) * 100)
			end
		end)
	end
end)

end
