local skynet = require "skynet"
local coroutine = coroutine
local pcall = pcall
local table = table

function skynet.queue()
	local current_thread
	local ref = 0
	local thread_queue = {}
	return function(f, ...)
		local thread = coroutine.running()
		if ref == 0 then
			current_thread = thread
		elseif current_thread ~= thread then
			table.insert(thread_queue, thread)
			skynet.wait()
			assert(ref == 0)
		end
		ref = ref + 1
		local ok, err = pcall(f, ...)
		ref = ref - 1
		if ref == 0 then
			current_thread = nil
			local co = table.remove(thread_queue,1)
			if co then
				skynet.wakeup(co)
			end
		end
		assert(ok,err)
	end
end

return skynet.queue