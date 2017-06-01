local skynet = require "skynet"
local dns = require "skynet.dns"

skynet.start(function()
	print("nameserver:", dns.server())	-- set nameserver
	-- you can specify the server like dns.server("8.8.4.4", 53)
	local ip, ips = dns.resolve "github.com"
	for k,v in ipairs(ips) do
		print("github.com",v)
	end
end)
