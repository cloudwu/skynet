local table = table

local httpd = {}

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

local function recvheader(readbytes, limit, lines, header)
	if #header >= 2 then
		if header:find "^\r\n" then
			return header:sub(3)
		end
	end
	local result
	local e = header:find("\r\n\r\n", 1, true)
	if e then
		result = header:sub(e+4)
	else
		while true do
			local bytes = readbytes()
			header = header .. bytes
			if #header > limit then
				return
			end
			e = header:find("\r\n\r\n", -#bytes-3, true)
			if e then
				result = header:sub(e+4)
				break
			end
			if header:find "^\r\n" then
				return header:sub(3)
			end
		end
	end
	for v in header:gmatch("(.-)\r\n") do
		if v == "" then
			break
		end
		table.insert(lines, v)
	end
	return result
end

local function parseheader(lines, from, header)
	local name, value
	for i=from,#lines do
		local line = lines[i]
		if line:byte(1) == 9 then	-- tab, append last line
			if name == nil then
				return
			end
			header[name] = header[name] .. line:sub(2)
		else
			name, value = line:match "^(.-):%s*(.*)"
			if name == nil or value == nil then
				return
			end
			name = name:lower()
			if header[name] then
				header[name] = header[name] .. ", " .. value
			else
				header[name] = value
			end
		end
	end
	return header
end

local function chunksize(readbytes, body)
	while true do
		if #body > 128 then
			return
		end
		body = body .. readbytes()
		local f,e = body:find("\r\n",1,true)
		if f then
			return tonumber(body:sub(1,f-1),16), body:sub(e+1)
		end
	end
end

local function readcrln(readbytes, body)
	if #body > 2 then
		if body:sub(1,2) ~= "\r\n" then
			return
		end
		return body:sub(3)
	else
		body = body .. readbytes(2-#body)
		if body ~= "\r\n" then
			return
		end
		return ""
	end
end

local function recvchunkedbody(readbytes, bodylimit, header, body)
	local result = ""
	local size = 0

	while true do
		local sz
		sz , body = chunksize(readbytes, body)
		if not sz then
			return
		end
		if sz == 0 then
			break
		end
		size = size + sz
		if bodylimit and size > bodylimit then
			return
		end
		if #body >= sz then
			result = result .. body:sub(1,sz)
			body = body:sub(sz+1)
		else
			result = result .. body .. readbytes(sz - #body)
			body = ""
		end
		body = readcrln(readbytes, body)
		if not body then
			return
		end
	end

	local tmpline = {}
	body = recvheader(readbytes, 8192, tmpline, body)
	if not body then
		return
	end

	header = parseheader(tmpline,1,header)

	return result, header
end

local function readall(readbytes, bodylimit)
	local tmpline = {}
	local body = recvheader(readbytes, 8192, tmpline, "")
	if not body then
		return 413	-- Request Entity Too Large
	end
	local request = assert(tmpline[1])
	local method, url, httpver = request:match "^(%a+)%s+(.-)%s+HTTP/([%d%.]+)$"
	assert(method and url and httpver)
	httpver = assert(tonumber(httpver))
	if httpver < 1.0 or httpver > 1.1 then
		return 505	-- HTTP Version not supported
	end
	local header = parseheader(tmpline,2,{})
	if not header then
		return 400	-- Bad request
	end
	local length = header["content-length"]
	if length then
		length = tonumber(length)
	end
	local mode = header["transfer-encoding"]
	if mode then
		if mode ~= "identity" and mode ~= "chunked" then
			return 501	-- Not Implemented
		end
	end

	if mode == "chunked" then
		body, header = recvchunkedbody(readbytes, bodylimit, header, body)
		if not body then
			return 413
		end
	else
		-- identity mode
		if length then
			if length > bodylimit then
				return 413
			end
			if #body >= length then
				body = body:sub(1,length)
			else
				local padding = readbytes(length - #body)
				body = body .. padding
			end
		end
	end

	return 200, url, method, header, body
end

function httpd.read_request(...)
	local ok, code, url, method, header, body = pcall(readall, ...)
	if ok then
		return code, url, method, header, body
	else
		return nil, code
	end
end

local function writeall(writefunc, statuscode, bodyfunc, header)
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

function httpd.write_response(...)
	return pcall(writeall, ...)
end

return httpd
