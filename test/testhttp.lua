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

local function http_keep_alive_test()
	httpc.timeout = 100
	httpc.keep_alive_time = 3

	local function loop_request()
		for i = 1,3 do
			local respheader = {}
			local status, body ,handle = httpc.keep_alive_request('GET','http://baidu.com','/',respheader)
			if i == 1 then
				setmetatable(handle,{__gc = function(hdl) print("gc fd = ",hdl.fd) end})
			end
			print("-----------------",i,"------------------------")
			print("[header] =====>")
			for k,v in pairs(respheader) do
				print(k,v)
			end
			print("[body] =====>", status)
			print(body)
			print("request by fd = ",handle.fd)
			print("-----------------",i,"------------------------")
			skynet.sleep(200)
		end
	end
	loop_request()
	skynet.sleep(500)
	loop_request()
	skynet.send(skynet.self(),'debug','GC')
	skynet.sleep(500)
	skynet.send(skynet.self(),'debug','GC')
end

local function main()
	dns.server()

	http_stream_test()
	http_head_test()
	http_keep_alive_test()

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
 