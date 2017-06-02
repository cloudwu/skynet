local skynet = require "skynet"
local snax = require "skynet.snax"

skynet.start(function()
	local ps = snax.uniqueservice ("pingserver", "test queue")
	for i=1, 10 do
		ps.post.sleep(true,i*10)
		ps.post.hello()
	end
	for i=1, 10 do
		ps.post.sleep(false,i*10)
		ps.post.hello()
	end

	skynet.exit()
end)


