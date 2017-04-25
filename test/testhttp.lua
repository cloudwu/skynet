local skynet = require "skynet"
local httpc = require "http.httpc"
local dns = require "dns"

local function main()
	httpc.dns()	-- set dns server
	httpc.timeout = 100	-- set timeout 1 second
	print("GET baidu.com")
	local respheader = {}
	local status, body = httpc.get("baidu.com", "/", respheader)
	print("[header] =====>")
	for k,v in pairs(respheader) do
		print(k,v)
	end
	print("[body] =====>", status)
	print(body)

	local respheader = {}
	dns.server()
	local ip = dns.resolve "baidu.com"
	print(string.format("GET %s (baidu.com)", ip))
	local status, body = httpc.get("baidu.com", "/", respheader, { host = "baidu.com" })
	print(status)
end

skynet.start(function()
	print(pcall(main))
	skynet.exit()
end)
