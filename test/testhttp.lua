local skynet = require "skynet"
local httpc = require "http.httpc"
local httpurl = require "http.url"
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

local function http_stream_test()
	for resp, stream in httpc.request_stream("GET", "http://baidu.com", "/") do
		print("STATUS", stream.status)
		for k,v in pairs(stream.header) do
			print("HEADER",k,v)
		end
		print("BODY", resp)
	end
end

local function http_head_test()
	httpc.timeout = 100
	local respheader = {}
	local status = httpc.head("http://baidu.com", "/", respheader)
	for k,v in pairs(respheader) do
		print("HEAD", k, v)
	end
end

local function http_url_test()
	local url = "http://baidu.com/get?k1=1&k2=2&k4=a%20space&k5=b%20space&k5=b%20space&k5=b%20space"
	local path, query = httpurl.parse(url)
	print("url", path, query)
	local qret = httpurl.parse_query(query)
	for k, v in pairs(qret) do
		print(k, v)
	end
	assert(#qret["k5"] == 3)
	assert(qret[1] == qret[2])
	assert(qret[1] == qret[3])
end

local function main()
	dns.server()

	http_stream_test()
	http_head_test()
	http_url_test()

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
 