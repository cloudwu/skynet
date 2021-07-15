local skynet = require "skynet"
local coroutine = coroutine
local xpcall = xpcall
local traceback = debug.traceback
local table = table

function skynet.queue2()
	local current_thread
	local ref = 0
	local thread_queue = {}

	local scope_lock = setmetatable({}, { __close = function()
		ref = ref - 1
		if ref == 0 then
			current_thread = table.remove(thread_queue,1)
			if current_thread then
				skynet.wakeup(current_thread)
			end
		end
	end})

	return function()
		local thread = coroutine.running()
		if current_thread and current_thread ~= thread then
			table.insert(thread_queue, thread)
			skynet.wait()
			assert(ref == 0)	-- current_thread == thread
		end
		current_thread = thread

		ref = ref + 1
		return scope_lock
	end
end

return skynet.queue2
