local skynet = require "skynet"
local dns = require "skynet.dns"

local resolve_list = {
	"github.com",
	"stackoverflow.com",
	"lua.com",
}

skynet.start(function()
	-- you can specify the server like dns.server("8.8.4.4", 53)
	for _ , name in ipairs(resolve_list) do
		local ip, ips = dns.resolve(name)
		for k,v in ipairs(ips) do
			print(name,v)
		end
		skynet.sleep(500)	-- sleep 5 sec
	end
end)
