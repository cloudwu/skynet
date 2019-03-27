local skynet = require "skynet"
local httpc = require "http.httpc"
local dns = require "skynet.dns"

local function http_test(protocol)
	--httpc.dns()	-- set dns server
	httpc.timeout = 100	-- set timeout 1 second
	print("GET baidu.com")
	protocol = protocol or "http"
	local respheader = {}
	local host = string.format("%s://baidu.com", protocol)
	print("geting... ".. host)
	local status, body = httpc.get(host, "/", respheader)
	print("[header] =====>")
	for k,v in pairs(respheader) do
		print(k,v)
	end
	print("[body] =====>", status)
	print(body)

	local respheader = {}
	local ip = dns.resolve "baidu.com"
	print(string.format("GET %s (baidu.com)", ip))
	local status, body = httpc.get(host, "/", respheader, { host = "baidu.com" })
	print(status)
end


local function main()
	dns.server()
	http_test("http")

	if not pcall(require,"ltls.c") then
		print "No ltls module, https is not supported"
	else
		http_test("https")
	end
end

skynet.start(function()
	print(pcall(main))
	skynet.exit()
end)
