local skynet = require "skynet"


skynet.start(function()
	print("Main Server start")
	local console = skynet.newservice(
		"testmongodb", "127.0.0.1", 27017, "testdb", "test", "test"
	)
	
	print("Main Server exit")
	skynet.exit()
end)
