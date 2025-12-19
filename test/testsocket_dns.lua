local skynet = require "skynet"
local socket = require "skynet.socket"
local driver = require "skynet.socketdriver"
local dns = require "skynet.dns"

local function test_socket_open_with_dns()
	-- Test DNS resolution
	local id = socket.open("www.baidu.com", 80)
	assert(id, "Failed to connect to www.baidu.com")
	socket.close(id)

	-- Test IP connection
	id = socket.open("8.8.8.8", 53)
	assert(id, "Failed to connect to 8.8.8.8")
	socket.close(id)
end

local function test_invalid_domain()
	-- Should fail for invalid domain
	local status, result = pcall(socket.open, "nonexistent.invalid.domain", 80)
	assert(not (status and result), "Should not connect to invalid domain")
end

local function test_concurrent_dns_resolution()
	local domains = {"www.github.com", "www.google.com", "www.baidu.com"}
	local results = {}

	for i, domain in ipairs(domains) do
		skynet.fork(function()
			local id = socket.open(domain, 80)
			results[i] = id ~= nil
			if id then socket.close(id) end
		end)
	end

	skynet.sleep(300) -- Wait for completion

	-- Verify most connections succeeded (async DNS should work)
	local success_count = 0
	for _, success in ipairs(results) do
		if success then success_count = success_count + 1 end
	end
	assert(success_count >= 2, "Too many concurrent DNS failures")
end

local function test_udp_dns_resolution()
	local response_received = false
	local id = socket.udp(function(str, from)
		response_received = #str >= 12 -- Valid DNS response
	end)

	socket.udp_connect(id, "dns.google", 53)

	-- Simple DNS query for google.com
	local dns_query = string.char(0xaa, 0xbb, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00) ..
		"\x06google\x03com\x00" .. string.char(0x00, 0x01, 0x00, 0x01)

	socket.write(id, dns_query)

	-- Wait for response
	for i = 1, 20 do
		if response_received then break end
		skynet.sleep(10)
	end

	socket.close(id)
	assert(response_received, "UDP DNS query failed")
end

local function main()
	dns.server("8.8.8.8", 53)

	print("Testing basic DNS resolution...")
	test_socket_open_with_dns()

	print("Testing invalid domain handling...")
	test_invalid_domain()

	print("Testing concurrent DNS resolution...")
	test_concurrent_dns_resolution()

	print("Testing UDP DNS resolution...")
	test_udp_dns_resolution()

	print("All DNS tests passed!")
	skynet.exit()
end

skynet.start(main)
