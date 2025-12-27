local skynet = require "skynet"
local socket = require "http.sockethelper"
local internal = require "http.internal"
local dns = require "skynet.dns"

local string = string
local table = table
local pcall = pcall
local error = error
local pairs = pairs

local httpc = {}


local async_dns

function httpc.dns(server,port)
	async_dns = true
	dns.server(server,port)
end

local function check_protocol(host)
	local protocol, hostname = host:match "^(%a+)://(.*)"
	if protocol then
		return string.lower(protocol), hostname
	else
		return "http", host
	end
end

local SSLCTX_CLIENT = nil
local function gen_interface(protocol, fd, hostname)
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
		local tls_ctx = tls.newtls("client", SSLCTX_CLIENT, hostname)
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

local default_port = {
	http = 80,
	https = 443,
}

local function connect(host, timeout)
	local protocol, port
	protocol, host = check_protocol(host)
	if not host:find ":%d+$" then
		port = default_port[protocol] or error("Invalid protocol: " .. protocol)
	end
	if async_dns then
		local hostname
		if port then -- without :%d+$
			-- if ipv6, contains colons
			-- if ipv4, end with a digit
			if host:find "^[^:]-[^%d]$" then
				hostname = host
			end
		else -- with :%d+$ (port)
			-- hostname string (ends with ":%d+") must begin with a substring that doesn't contain colon "[^:]"
			-- and end with a character that is not a colon or a digit "[^%d%]]".
			hostname, port = host:match "^([^:]-[^%d%]]):(%d+)$"
		end
        if hostname then
            local msg
            host, msg = dns.resolve(hostname)
            if not host then
                error(string.format("%s dns resolve failed msg:%s", hostname, msg))
            end
        end
	end

	local fd = socket.connect(host, port, timeout)
	if not fd then
		error(string.format("%s connect error host:%s, port:%s, timeout:%s", protocol, host, port, timeout))
	end
	local interface = gen_interface(protocol, fd, hostname)
	if timeout then
		skynet.timeout(timeout, function()
			if not interface.finish then
				socket.shutdown(fd)	-- shutdown the socket fd, need close later.
			end
		end)
	end
	if interface.init then
		interface.init(host)
	end
	return fd, interface, host
end

local function close_interface(interface, fd)
	interface.finish = true
	socket.close(fd)
	if interface.close then
		interface.close()
		interface.close = nil
	end
end

function httpc.request(method, hostname, url, recvheader, header, content)
	local fd, interface, host = connect(hostname, httpc.timeout)
	local ok , statuscode, body , header = pcall(internal.request, interface, method, host, url, recvheader, header, content)
	if ok then
		ok, body = pcall(internal.response, interface, statuscode, body, header)
	end
	close_interface(interface, fd)
	if ok then
		return statuscode, body
	else
		error(body or statuscode)
	end
end

function httpc.head(hostname, url, recvheader, header, content)
	local fd, interface, host = connect(hostname, httpc.timeout)
	local ok , statuscode = pcall(internal.request, interface, "HEAD", host, url, recvheader, header, content)
	close_interface(interface, fd)
	if ok then
		return statuscode
	else
		error(statuscode)
	end
end

function httpc.request_stream(method, hostname, url, recvheader, header, content)
	local fd, interface, host = connect(hostname, httpc.timeout)
	local ok , statuscode, body , header = pcall(internal.request, interface, method, host, url, recvheader, header, content)
	interface.finish = true -- don't shutdown fd in timeout
	local function close_fd()
		close_interface(interface, fd)
	end
	if not ok then
		close_fd()
		error(statuscode)
	end
	-- todo: stream support timeout
	local stream = internal.response_stream(interface, statuscode, body, header)
	stream._onclose = close_fd
	return stream
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
