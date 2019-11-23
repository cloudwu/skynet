local skynet = require "skynet"

local service_address

local function start_service(max_threads)
	service_address = skynet.uniqueservice("blockingfile", max_threads)
end

local function ensure_start_service()
	if not service_address then
		start_service(4)
	end
	return service_address
end

local function blocking_open(filename, mode)
	return skynet.call(ensure_start_service(), "lua", "open", filename, mode)
end

local function blocking_read(handle, fmt)
	return skynet.call(ensure_start_service(), "lua", "read", handle, fmt)
end

local function blocking_seek(handle, whence, offset)
	return skynet.call(ensure_start_service(), "lua", "seek", handle, whence, offset)
end

local function blocking_close(handle)
	return skynet.call(ensure_start_service(), "lua", "close", handle)
end

local function blocking_write(handle, data)
	return skynet.call(ensure_start_service(), "lua", "write", handle, data)
end

local function blocking_flush(handle)
	return skynet.call(ensure_start_service(), "lua", "flush", handle)
end

return {
	open = blocking_open,
	read = blocking_read,
	seek = blocking_seek,
	write = blocking_write,
	flush = blocking_flush,
	close = blocking_close,

	start_service = start_service,
}
