local socket = require "http.sockethelper"
local url = require "http.url"
local internal = require "http.internal"
local string = string
local table = table

local httpc = {}

local function request(fd, method, host, url, recvheader, header, content)
	local read = socket.readfunc(fd)
	local write = socket.writefunc(fd)
	local header_content = ""
	if header then
		for k,v in pairs(header) do
			header_content = string.format("%s%s:%s\r\n", header_content, k, v)
		end
		if header.host then
			host = ""
		end
	else
		host = string.format("host:%s\r\n",host)
	end

	if content then
		local data = string.format("%s %s HTTP/1.1\r\n%scontent-length:%d\r\n%s\r\n%s", method, url, host, #content, header_content, content)
		write(data)
	else
		local request_header = string.format("%s %s HTTP/1.1\r\nhost:%s\r\ncontent-length:0\r\n%s\r\n", method, url, host, header_content)
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
			body = nil
		end
	end

	return code, body
end

function httpc.request(method, host, url, recvheader, header, content)
	local hostname, port = host:match"([^:]+):?(%d*)$"
	if port == "" then
		port = 80
	else
		port = tonumber(port)
	end
	local fd = socket.connect(hostname, port)
	local ok , statuscode, body = pcall(request, fd,method, host, url, recvheader, header, content)
	socket.close(fd)
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
