local skynet = require "skynet"
local socket = require "http.sockethelper"
local url = require "http.url"
local internal = require "http.internal"
local dns = require "skynet.dns"
local string = string
local table = table

local httpc = {}


local async_dns

function httpc.dns(server,port)
	async_dns = true
	dns.server(server,port)
end


local function check_protocol(host)
	local protocol = host:match("^[Hh][Tt][Tt][Pp][Ss]?://")
	if protocol then
		host = string.gsub(host, "^"..protocol, "")
		protocol = string.lower(protocol)
		if protocol == "https://" then
			return "https", host
		elseif protocol == "http://" then
			return "http", host
		else
			error(string.format("Invalid protocol: %s", protocol))
		end
	else
		return "http", host
	end
end

local SSLCTX_CLIENT = nil
local function gen_interface(protocol, fd)
	if protocol == "http" then
		return {
			init = nil,
			close = nil,
			read = socket.readfunc(fd),
			write = socket.writefunc(fd),
			readall = function ()
				return socket.readall(fd)
			end,
		}
	elseif protocol == "https" then
		local tls = require "http.tlshelper"
		SSLCTX_CLIENT = SSLCTX_CLIENT or tls.newctx()
		local tls_ctx = tls.newtls("client", SSLCTX_CLIENT)
		return {
			init = tls.init_requestfunc(fd, tls_ctx),
			close = tls.closefunc(tls_ctx),
			read = tls.readfunc(fd, tls_ctx),
			write = tls.writefunc(fd, tls_ctx),
			readall = tls.readallfunc(fd, tls_ctx),
		}
	else
		error(string.format("Invalid protocol: %s", protocol))
	end
end


function httpc.request(method, host, url, recvheader, header, content)
	local protocol
	local timeout = httpc.timeout	-- get httpc.timeout before any blocked api
	protocol, host = check_protocol(host)
	local hostname, port = host:match"([^:]+):?(%d*)$"
	if port == "" then
		port = protocol=="http" and 80 or protocol=="https" and 443
	else
		port = tonumber(port)
	end
	if async_dns and not hostname:match(".*%d+$") then
		hostname = dns.resolve(hostname)
	end
	local fd = socket.connect(hostname, port, timeout)
	if not fd then
		error(string.format("%s connect error host:%s, port:%s, timeout:%s", protocol, hostname, port, timeout))
		return
	end
	-- print("protocol hostname port", protocol, hostname, port)
	local interface = gen_interface(protocol, fd)
	local finish
	if timeout then
		skynet.timeout(timeout, function()
			if not finish then
				socket.shutdown(fd)	-- shutdown the socket fd, need close later.
				if interface.close then
					interface.close()
				end
			end
		end)
	end
	if interface.init then
		interface.init()
	end
	local ok , statuscode, body = pcall(internal.request, interface, method, host, url, recvheader, header, content)
	finish = true
	socket.close(fd)
	if interface.close then
		interface.close()
	end
	if ok then
		return statuscode, body
	else
		error(statuscode)
	end
end

function httpc.get(...)
	return httpc.request("GET", ...)
end

local function escape(s)
	return (string.gsub(s, "([^A-Za-z0-9_])", function(c)
		return string.format("%%%02X", string.byte(c))
	end))
end

function httpc.post(host, url, form, recvheader)
	local header = {
		["content-type"] = "application/x-www-form-urlencoded"
	}
	local body = {}
	for k,v in pairs(form) do
		table.insert(body, string.format("%s=%s",escape(k),escape(v)))
	end

	return httpc.request("POST", host, url, recvheader, header, table.concat(body , "&"))
end

return httpc
