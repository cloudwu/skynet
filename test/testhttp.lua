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

-- Integration test: connect to real server with various URL formats
-- Verifies parse_host / parse_url via httpc public API
local function test_connect_formats()
	print("[test_connect_formats] =====>")
	httpc.timeout = 100

	local passed, total = 0, 0

	local function try_get(desc, host)
		local ok, status, body = pcall(httpc.get, host, "/", {})
		total = total + 1
		if ok then
			passed = passed + 1
			print(string.format("  PASS: %-35s status=%s", desc, status))
		else
			print(string.format("  FAIL: %-35s error=%s", desc, status))
		end
		return ok
	end

	-- verify parsing is correct even when connection fails
	-- "connect error" in the message means parsing succeeded
	local function try_parse(desc, host)
		local ok, err = pcall(httpc.get, host, "/", {})
		total = total + 1
		if ok then
			passed = passed + 1
			print(string.format("  PASS: %-35s connected", desc))
			return true
		end
		err = tostring(err)
		if err:find("connect error") or err:find("connect failed") or err:find("Socket Error") then
			passed = passed + 1
			print(string.format("  PASS: %-35s parsed ok (connect failed as expected)", desc))
			return true
		else
			print(string.format("  FAIL: %-35s error=%s", desc, err))
			return false
		end
	end

	-- verify that invalid input is properly rejected
	local function try_error(desc, host, pattern)
		local ok, err = pcall(httpc.get, host, "/", {})
		total = total + 1
		if ok then
			print(string.format("  FAIL: %-35s expected error but succeeded", desc))
			return false
		end
		err = tostring(err)
		if err:find(pattern) then
			passed = passed + 1
			print(string.format("  PASS: %-35s rejected as expected", desc))
			return true
		else
			print(string.format("  FAIL: %-35s wrong error=%s", desc, err))
			return false
		end
	end

	-- hostname variants
	try_get("bare hostname",            "baidu.com")
	try_get("hostname:port",            "baidu.com:80")
	try_get("http://hostname",          "http://baidu.com")
	try_get("http://hostname:port",     "http://baidu.com:80")
	try_get("HTTP:// uppercase",        "HTTP://baidu.com")

	-- ipv4 variants (resolve first to get a real IP)
	local ip = dns.resolve("baidu.com")
	if ip then
		print(string.format("  resolved baidu.com -> %s", ip))
		try_get("bare ipv4",                ip)
		try_get("ipv4:port",                ip .. ":80")
		try_get("http://ipv4",              "http://" .. ip)
		try_get("http://ipv4:port",         "http://" .. ip .. ":80")
	else
		print("  SKIP: ipv4 tests (dns resolve failed)")
	end

	-- https variants (skip if no tls)
	if pcall(require, "ltls.c") then
		try_get("https://hostname",         "https://baidu.com")
		try_get("https://hostname:port",    "https://baidu.com:443")
		try_get("HTTPS:// uppercase",       "HTTPS://baidu.com")
	else
		print("  SKIP: https tests (no ltls module)")
	end

	-- bare ipv6 should be rejected (RFC 3986: brackets required)
	try_error("bare ipv6 ::1 (reject)",    "::1",         "bare IPv6")
	try_error("bare full ipv6 (reject)",   "2001:db8::1", "bare IPv6")

	-- bracketed ipv6 (parsing verification, connection may fail without ipv6 network)
	try_parse("[ipv6] no port",            "[::1]")
	try_parse("[ipv6]:port",              "[::1]:8080")
	try_parse("[full ipv6] no port",      "[2001:db8::1]")
	try_parse("[full ipv6]:port",         "[2001:db8::1]:80")
	try_parse("http://[ipv6]",            "http://[::1]")
	try_parse("http://[ipv6]:port",       "http://[::1]:80")
	if pcall(require, "ltls.c") then
		try_parse("https://[ipv6]:port",   "https://[::1]:443")
	end

	-- ipv6 real connection test via AAAA DNS resolution
	local ok_aaaa, ipv6 = pcall(dns.resolve, "ipv6.google.com", true)
	if ok_aaaa and ipv6 then
		local ipv6_host = string.format("[%s]", ipv6)
		print(string.format("  resolved ipv6.google.com AAAA -> %s", ipv6))
		try_parse("[ipv6] from AAAA",          ipv6_host)
		try_parse("[ipv6]:80 from AAAA",       ipv6_host .. ":80")
		try_parse("http://[ipv6] from AAAA",    "http://" .. ipv6_host)
		try_parse("http://[ipv6]:80 from AAAA", "http://" .. ipv6_host .. ":80")
	else
		print("  SKIP: ipv6 AAAA tests (dns resolve failed for ipv6.google.com)")
	end

	-- k8s internal domain variants (parsing verification, connection will fail outside k8s)
	-- domains ending with digits must not be mistaken for ipv4
	try_parse("k8s svc short",            "my-svc:8080")
	try_parse("k8s svc.ns",               "my-svc.default:8080")
	try_parse("k8s svc.ns.svc",           "my-svc.default.svc:8080")
	try_parse("k8s svc fqdn",             "my-svc.default.svc.cluster.local:8080")
	try_parse("k8s svc fqdn no port",     "my-svc.default.svc.cluster.local")
	try_parse("k8s http:// svc fqdn",     "http://my-svc.default.svc.cluster.local:8080")
	try_parse("k8s headless pod",         "pod-0.my-svc.default.svc.cluster.local:8080")
	try_parse("k8s nodeport",             "http://node1.cluster.local:30080")
	try_parse("k8s statefulset pod-0",    "web-0.nginx-svc.default:8080")
	try_parse("k8s numeric ns",           "my-svc.ns123:8080")
	try_parse("k8s all-digit segments",   "svc123.ns456:9090")
	try_parse("k8s pod ip-style dns",     "10-244-0-5.default.pod.cluster.local:80")
	try_parse("k8s 5-segment digits",     "10.0.0.1.nip.io:8080")

	print(string.format("[test_connect_formats] done, %d/%d passed", passed, total))
end

local function main()
	dns.server()

	test_connect_formats()

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
