local skynet = require "skynet"
local httpc = require "http.httpc"

skynet.start(function()
	print("GET baidu.com")
	local header = {}
	local status, body = httpc.get("baidu.com", "/", header)
	print("[header] =====>")
	for k,v in pairs(header) do
		print(k,v)
	end
	print("[body] =====>", status)
	print(body)

	skynet.exit()
end)
