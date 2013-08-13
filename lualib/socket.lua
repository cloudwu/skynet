local buffer = require "socketbuffer"
local skynet = require "skynet"
local table = table
local next = next
local assert = assert
local coroutine = coroutine
local type = type

local READBUF = {}	-- fd:buffer
local READREQUEST = {}	-- fd:request_size
local READSESSION = {}	-- fd:session
local READLOCK = {}	-- fd:queue(session)
local READTHREAD= {} -- fd:thread
local CLOSED = {} -- fd:true

local selfaddr = skynet.self()
local sockets = assert(skynet.localname ".socket")

local function response(session)
	skynet.redirect(selfaddr , 0, "response", session, "")
end

skynet.register_protocol {
	name = "client",
	id = 3,	-- PTYPE_CLIENT
	pack = buffer.pack,
	unpack = buffer.unpack,
	dispatch = function (_, _, fd, msg, sz)
		local qsz = READREQUEST[fd]
		local buf = READBUF[fd]
		local bsz
		if sz == 0 then
			CLOSED[fd] = true
		else
			buf,bsz = buffer.push(buf, msg, sz)
			READBUF[fd] = buf
		end
		local session = READSESSION[fd]
		if qsz == nil or session == nil then
			return
		end
		if sz > 0 then
			if type(qsz) == "number" then
				if qsz > bsz then
					return
				end
			else
				-- request readline
				if buffer.readline(buf, qsz, true) == nil then
					return
				end
			end
		end

		response(session)
		READSESSION[fd] = nil
	end
}

skynet.register_protocol {
	name = "system",
	id = 4, -- PTYPE_SYSTEM
	pack = skynet.pack,
	unpack = function (...) return ... end,
	dispatch = function (session, addr, msg, sz)
		fd, t, sz = skynet.unpack(msg,sz)
		assert(addr == selfaddr, "PTYPE_SYSTEM message must send by self")
		if t > 0 then	-- lock request when t == 0
			-- request bytes or readline
			local buf = READBUF[fd]
			if CLOSED[fd] then
				skynet.ret()
				return
			end
			local _,bsz = buffer.push(buf)
			if t == 1 then
				-- sz is size
				if bsz >= sz then
					skynet.ret()
					return
				end
			elseif t == 2 then
				-- sz is sep
				if buffer.readline(buf, sz, true) then -- don't real read
					skynet.ret()
					return
				end
			end
			READSESSION[fd] = session
		end
	end
}

local socket = {}

function socket.open(addr, port)
	local cmd = "open" .. " " .. (port and (addr..":"..port) or addr)
	local r = skynet.call(sockets, "text", cmd)
	if r == "" then
		return nil,  cmd .. " failed"
	end
	local fd = tonumber(r)
	READBUF[fd] = true
	CLOSED[fd] = nil
	return fd
end

function socket.bind(sock)
	local r = skynet.call(sockets, "text", "bind " .. tonumber(sock))
	if r == "" then
		error("stdin bind failed")
	end
	local fd = tonumber(r)
	READBUF[fd] = true
	CLOSED[fd] = nil
	return fd
end

function socket.stdin()
	return socket.bind(1)
end

function socket.close(fd)
	socket.lock(fd)
	skynet.call(sockets, "text", "close", fd)
	READBUF[fd] = nil
	READLOCK[fd] = nil
	CLOSED[fd] = nil
end

function socket.read(fd, sz)
	local buf = assert(READBUF[fd])
	local str, bytes = buffer.pop(buf,sz)
	if str then
		return str
	end

	if CLOSED[fd] then
		READBUF[fd] = nil
		CLOSED[fd] = nil
		str = buffer.pop(buf, bytes)
		return nil, str
	end

	READREQUEST[fd] = sz
	skynet.call(selfaddr, "system",fd,1,sz)	-- singal size 1
	READREQUEST[fd] = nil

	buf = READBUF[fd]

	str, bytes = buffer.pop(buf,sz)
	if str then
		return str
	end

	if CLOSED[fd] then
		READBUF[fd] = nil
		CLOSED[fd] = nil
		str = buffer.pop(buf, bytes)
		return nil, str
	end
end

function socket.readall(fd)
	local buf = assert(READBUF[fd])
	if CLOSED[fd] then
		CLOSED[fd] = nil
		READBUF[fd] = nil
		if buf == nil then
			return ""
		end
		local _, bytes = buffer.push(buf)
		local ret = buffer.pop(buf, bytes)
		return ret
	end
	READREQUEST[fd] = math.huge
	skynet.call(selfaddr, "system",fd,3)	-- singal readall
	READREQUEST[fd] = nil
	assert(CLOSED[fd])
	buf = READBUF[fd]
	READBUF[fd] = nil
	CLOSED[fd] = nil
	if buf == nil then
		return ""
	end
	local _, bytes = buffer.push(buf)
	local ret = buffer.pop(buf, bytes)
	return ret
end

function socket.readline(fd, sep)
	local buf = assert(READBUF[fd])
	local str = buffer.readline(buf,sep)
	if str then
		return str
	end

	if CLOSED[fd] then
		READBUF[fd] = nil
		CLOSED[fd] = nil
		local _, bytes = buffer.push(buf)
		str = buffer.pop(buf, bytes)
		return nil, str
	end

	READREQUEST[fd] = sep
	skynet.call(selfaddr, "system",fd,2,sep)	-- singal sep 2
	READREQUEST[fd] = nil

	buf = READBUF[fd]
	str = buffer.readline(buf,sep)
	if str then
		return str
	end

	if CLOSED[fd] then
		READBUF[fd] = nil
		CLOSED[fd] = nil
		local _, bytes = buffer.push(buf)
		str = buffer.pop(buf, bytes)
		return nil, str
	end
end

function socket.write(fd, msg, sz)
	if CLOSED[fd] or not READBUF[fd] then
		return
	end
	skynet.send(sockets, "client", fd, msg, sz)
	return true
end

function socket.invalid(fd)
	return CLOSED[fd] or not READBUF[fd]
end

function socket.lock(fd)
	if CLOSED[fd] or not READBUF[fd] then
		return
	end
	local locked = READTHREAD[fd]
	if locked then
		-- lock fd
		local session = skynet.genid()
		local q = READLOCK[fd]
		if q == nil then
			READLOCK[fd] = { session }
		else
			table.insert(q, session)
		end

		skynet.redirect(selfaddr , 0, "system", session, skynet.pack(fd,0))
		coroutine.yield("CALL",session)
	else
		READTHREAD[fd] = true
	end
end

function socket.unlock(fd)
	READTHREAD[fd] = nil
	local q = READLOCK[fd]
	if q then
		if q[1] then
			READTHREAD[fd] = true
			response(q[1])
			table.remove(q,1)
		else
			READLOCK[fd] = nil
		end
	end
end

return socket
