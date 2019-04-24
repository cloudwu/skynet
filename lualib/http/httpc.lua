local skynet = require "skynet"
local socket = require "http.sockethelper"
local url = require "http.url"
local internal = require "http.internal"
local dns = require "skynet.dns"
local string = string
local table = table

local httpc = {}

local function request(interface, method, host, url, recvheader, header, content)
	local read = interface.read
	local write = interface.write
	local header_content = ""
	if header then
		if not header.host then
			header.host = host
		end
		for k,v in pairs(header) do
			header_content = string.format("%s%s:%s\r\n", header_content, k, v)
		end
	else
		header_content = string.format("host:%s\r\n",host)
	end

	if content then
		local data = string.format("%s %s HTTP/1.1\r\n%scontent-length:%d\r\n\r\n", method, url, header_content, #content)
		write(data)
		write(content)
	else
		local request_header = string.format("%s %s HTTP/1.1\r\n%scontent-length:0\r\n\r\n", method, url, header_content)
		write(request_header)
	end

	local tmpline = {}
	local body = internal.recvheader(read, tmpline, "")
	if not body then
		error(socket.socket_error)
	end

	local statusline = tmpline[1]
	local code, info = statusline:match "HTTP/[%d%.]+%s+([%d]+)%s+(.*)$"
	code = assert(tonumber(code))

	local header = internal.parseheader(tmpline,2,recvheader or {})
	if not header then
		error("Invalid HTTP response header")
	end

	local length = header["content-length"]
	if length then
		length = tonumber(length)
	end
	local mode = header["transfer-encoding"]
	if mode then
		if mode ~= "identity" and mode ~= "chunked" then
			error ("Unsupport transfer-encoding")
		end
	end

	if mode == "chunked" then
		body, header = internal.recvchunkedbody(read, nil, header, body)
		if not body then
			error("Invalid response body")
		end
	else
		-- identity mode
		if length then
			if #body >= length then
				body = body:sub(1,length)
			else
				local padding = read(length - #body)
				body = body .. padding
			end
		else
			-- no content-length, read all
			body = body .. interface.readall()
		end
	end

	return code, body
end

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
	local ok , statuscode, body = pcall(request, interface, method, host, url, recvheader, header, content)
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
