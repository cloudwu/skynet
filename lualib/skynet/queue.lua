local skynet = require "skynet"
local coroutine = coroutine
local xpcall = xpcall
local traceback = debug.traceback
local table = table

function skynet.queue()
	local current_thread
	local ref = 0
	local thread_queue = {}
	return function(f, ...)
		local thread = coroutine.running()
		if current_thread and current_thread ~= thread then
			table.insert(thread_queue, thread)
			skynet.wait()
			assert(ref == 0)	-- current_thread == thread
		end
		current_thread = thread

		ref = ref + 1
		local result = table.pack(xpcall(f, traceback, ...))
		ref = ref - 1
		if ref == 0 then
			current_thread = table.remove(thread_queue, 1)
			if current_thread then
				skynet.wakeup(current_thread)
			end
		end
    		local ok = table.remove(result, 1)
    		assert(ok, result[1])
    		return table.unpack(result)
	end
end

return skynet.queue
