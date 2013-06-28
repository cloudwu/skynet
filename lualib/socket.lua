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

local selfaddr = skynet.self()

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
		if sz == 0 or buf == true then
			buf,bsz = true, qsz
		else
			buf,bsz = buffer.push(buf, msg, sz)
		end
		READBUF[fd] = buf
		local session = READSESSION[fd]
		if qsz == nil or session == nil then
			return
		end
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
			if buf == true then
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
			else
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
	local r = skynet.call(".socket", "text", cmd)
	if r == "" then
		error(cmd .. " failed")
	end
	return tonumber(r)
end

function socket.stdin()
	local r = skynet.call(".socket", "text", "bind 1")
	if r == "" then
		error("stdin bind failed")
	end
	return tonumber(r)
end

function socket.close(fd)
	socket.lock(fd)
	skynet.call(".socket", "text", "close", fd)
	READBUF[fd] = true
	READLOCK[fd] = nil
end

function socket.read(fd, sz)
	local str = buffer.pop(READBUF[fd],sz)
	if str then
		return str
	end

	READREQUEST[fd] = sz
	skynet.call(selfaddr, "system",fd,1,sz)	-- singal size 1
	READREQUEST[fd] = nil

	str = buffer.pop(READBUF[fd],sz)
	return str
end

function socket.readline(fd, sep)
	local str = buffer.readline(READBUF[fd],sep)
	if str then
		return str
	end

	READREQUEST[fd] = sep
	skynet.call(selfaddr, "system",fd,2,sep)	-- singal sep 2
	READREQUEST[fd] = nil

	str = buffer.readline(READBUF[fd],sep)
	return str
end

function socket.write(fd, msg, sz)
	skynet.send(".socket", "client", fd, msg, sz)
end

function socket.lock(fd)
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
