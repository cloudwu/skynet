local skynet = require "skynet"
local httpc = require "http.httpc"
local server = require "web.httpserver"
skynet.start(function()
		print("http Server start")
		local web = server.listen(8001,{web_root="./lualib/web/"})
		local header = {}
		local status, body = httpc.get("127.0.0.1:8001", "/test/user/index", header)
		print("========",body)
		local status, body1 = httpc.get("127.0.0.1:8001", "/test.html", {})
		print("========",status,body1)
		local status, body1 = httpc.get("127.0.0.1:8001", "/test.html", {})
		skynet.sleep(5000)
		skynet.exit()
end)
