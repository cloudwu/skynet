local skynet = require "skynet"
local cluster = require "skynet.cluster"

skynet.start(function()
	skynet.fork(function()
		print("============================================1")
		cluster.reload {
			db = false,	-- db is down
			db3 = "127.0.0.1:2529"
		}
		print(cluster.call("db", "@sdb", "GET", "a"))
	end)
	skynet.fork(function()
		skynet.sleep(100)
		print("============================================2")
		cluster.reload {
			db = "127.0.0.1:2528",	
			db3 = "127.0.0.1:2529"
		}
		print(cluster.call("db", "@sdb", "GET", "a"))

		cluster.reload {
			db = false,	-- db is down
			db3 = "127.0.0.1:2529"
		}
		print(cluster.call("db", "@sdb", "GET", "a"))	-- db is down
	end
	)
	skynet.fork(function()
		skynet.sleep(200)
		print("============================================3")
		cluster.reload {
			db = "127.0.0.1:2528",
			db3 = "127.0.0.1:2529"
		}
		print(cluster.call("db", "@sdb", "SET", "a","hello"))
		print("===============",cluster.call("db", "@sdb", "GET", "a"))
	end
	)

end)
