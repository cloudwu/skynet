local table = table

local httpd = {}
local READLIMIT = 8192	-- limit bytes per read

local http_status_msg = {
	[100] = "Continue",
	[101] = "Switching Protocols",
	[200] = "OK",
	[201] = "Created",
	[202] = "Accepted",
	[203] = "Non-Authoritative Information",
	[204] = "No Content",
	[205] = "Reset Content",
	[206] = "Partial Content",
	[300] = "Multiple Choices",
	[301] = "Moved Permanently",
	[302] = "Found",
	[303] = "See Other",
	[304] = "Not Modified",
	[305] = "Use Proxy",
	[307] = "Temporary Redirect",
	[400] = "Bad Request",
	[401] = "Unauthorized",
	[402] = "Payment Required",
	[403] = "Forbidden",
	[404] = "Not Found",
	[405] = "Method Not Allowed",
	[406] = "Not Acceptable",
	[407] = "Proxy Authentication Required",
	[408] = "Request Time-out",
	[409] = "Conflict",
	[410] = "Gone",
	[411] = "Length Required",
	[412] = "Precondition Failed",
	[413] = "Request Entity Too Large",
	[414] = "Request-URI Too Large",
	[415] = "Unsupported Media Type",
	[416] = "Requested range not satisfiable",
	[417] = "Expectation Failed",
	[500] = "Internal Server Error",
	[501] = "Not Implemented",
	[502] = "Bad Gateway",
	[503] = "Service Unavailable",
	[504] = "Gateway Time-out",
	[505] = "HTTP Version not supported",
}

local function recvheader(readline, header)
	local line = readline()
	if line == "" then
		return header
	end

	local name, value
	repeat
		if line:byte(1) == 9 then	-- tab, append last line
			header[name] = header[name] .. line:sub(2)
		else
			name, value = line:match "^(.-):%s*(.*)"
			assert(name and value)
			name = name:lower()
			if header[name] then
				header[name] = header[name] .. ", " .. value
			else
				header[name] = value
			end
			line = readline()
		end
	until line == ""

	return header
end

local function recvbody(readbytes, length)
	if length < READLIMIT then
		return readbytes(length)
	end
	local tmp = {}
	while true do
		if length <= READLIMIT then
			table.insert(tmp, readbytes(length))
			break
		end
		table.insert(tmp, readbytes(READLIMIT))
		length = length - READLIMIT
	end
	return table.concat(tmp)
end

local function recvchunkedbody(readline, readbytes, header)
	local size = assert(tonumber(readline(),16))
	local body = recvbody(readbytes,size)
	assert(readbytes(2) == "\r\n")
	size = assert(tonumber(readline(),16))
	if size > 0 then
		local bodys = { body }
		repeat
			table.insert(bodys, recvbody(readbytes,size))
			assert(readbytes(2) == "\r\n")
			size = assert(tonumber(readline(),16))
		until size <= 0
		body = table.concat(bodys)
	end
	assert(readbytes(2) == "\r\n")
	header = recvheader(readline, header)
	return body, header
end

local function readall(readline, readbytes)
	local request = readline()
	local method, url, httpver = request:match "^(%a+)%s+(.-)%s+HTTP/([%d%.]+)$"
	assert(method and url and httpver)
	httpver = assert(tonumber(httpver))
	if httpver < 1.0 or httpver > 1.1 then
		return 505	-- HTTP Version not supported
	end
	local header = recvheader(readline, {})
	local length = header["content-length"]
	if length then
		length = tonumber(length)
	end
	local mode = header["transfer-encoding"]
	if mode then
		if mode ~= "identity" or mode ~= "chunked" then
			return 501	-- Not Implemented
		end
	end

	local body
	if mode == "chunked" then
		body, header = recvchunkedbody(readline, readbytes, header)
	else
		-- identity mode
		if length then
			body = readbody(readbytes, length)
		end
	end

	return 200, url, method, header, body
end

function httpd.read_request(readfunc)
	local readline = assert(readfunc.readline)
	local readbytes = assert(readfunc.readbytes)
	local ok, code, url, method, header, body = pcall(readall, readline, readbytes)
	if ok then
		return code, url, method, header, body
	else
		return nil, code
	end
end

function httpd.write_response(writefunc, statuscode, bodyfunc, header)
	local statusline = string.format("HTTP/1.1 %03d %s\r\n", statuscode, http_status_msg[statuscode] or "")
	writefunc(statusline)
	if header then
		for k,v in pairs(header) do
			writefunc(string.format("%s: %s\r\n", k,v))
		end
	end
	local t = type(bodyfunc)
	if t == "string" then
		writefunc(string.format("content-length: %d\r\n\r\n", #bodyfunc))
		writefunc(bodyfunc)
	elseif t == "function" then
		writefunc("transfer-encoding: chunked\r\n")
		while true do
			local s = bodyfunc()
			if s then
				if s ~= "" then
					writefunc(string.format("\r\n%x\r\n", #s))
					writefunc(s)
				end
			else
				writefunc("\r\n0\r\n\r\n")
			end
		end
	else
		assert(t == "nil")
		writefunc("\r\n")
	end
end

return httpd
