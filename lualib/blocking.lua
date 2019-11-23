local skynet = require "skynet"

local service_address

local function start_service(max_threads)
	service_address = skynet.uniqueservice("blocking", max_threads)
end

local function fetch_blocking_service(cb, timeout)
	if not service_address then
		start_service(4)
	end

	local addr, err = skynet.call(service_address, "lua", "get", tonumber(timeout) or 0)
	if not addr then
		return nil, err
	end
	local res = {cb(addr)}
	skynet.send(service_address, "lua", "put", addr)
	return table.unpack(res)
end

local function blocking_popen(cmdline, timeout)
	return fetch_blocking_service(function(addr)
		return skynet.call(addr, "lua", "popen", cmdline)
	end, timeout)
end

local function blocking_execute(cmdline, timeout)
	return fetch_blocking_service(function(addr)
		return skynet.call(addr, "lua", "execute", cmdline)
	end, timeout)
end

local function blocking_readfile(filename, timeout)
	return fetch_blocking_service(function(addr)
		return skynet.call(addr, "lua", "readfile", filename)
	end, timeout)
end

local function blocking_writefile(filename, data, timeout)
	return fetch_blocking_service(function(addr)
		return skynet.call(addr, "lua", "writefile", filename, data)
	end, timeout)
end

local function blocking_appendfile(filename, data, timeout)
	return fetch_blocking_service(function(addr)
		return skynet.call(addr, "lua", "appendfile", filename, data)
	end, timeout)
end

return {
	popen = blocking_popen,
	execute = blocking_execute,
	readfile = blocking_readfile,
	writefile = blocking_writefile,
	appendfile = blocking_appendfile,

	start_service = start_service,
}
