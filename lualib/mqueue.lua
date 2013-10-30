local skynet = require "skynet"
local c = require "skynet.c"

local mqueue = {}
local init_once
local thread_id
local message_queue = {}

skynet.register_protocol {
	name = "queue",
	id = 10,
	pack = skynet.pack,
	unpack = skynet.unpack,
	dispatch = function(session, from, ...)
		table.insert(message_queue, {..., session = session, addr = from})
		if thread_id then
			skynet.wakeup(thread_id)
			thread_id = nil
		end
	end
}

local function report_error(succ, ...)
	if succ then
		return ...
	else
		print("Message queue dispatch error: ", ...)
	end
end

local function do_func(f, msg)
	return report_error(pcall(f, table.unpack(msg)))
end

local function message_dispatch(f)
	while true do
		if #message_queue==0 then
			thread_id = coroutine.running()
			skynet.wait()
		else
			local msg = table.remove(message_queue,1)
			local session = msg.session
			if session == 0 then
				assert(do_func(f, msg) == nil, "message in queue returns value")
			else
				local data, size = skynet.pack(do_func(f,msg))
				-- 1 means response
				c.send(msg.addr, 1, session, data, size)
			end
		end
	end
end

function mqueue.register(f)
	assert(init_once == nil)
	init_once = true
	skynet.fork(message_dispatch,f)
end

function mqueue.call(addr, ...)
	return skynet.call(addr, "queue", ...)
end

function mqueue.send(addr, ...)
	return skynet.send(addr, "queue", ...)
end

function mqueue.size()
	return #message_queue
end

return mqueue
