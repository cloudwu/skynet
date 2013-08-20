local skynet = require "skynet"

local trace_cache = {}

skynet.trace_callback(function(handle, ti)
	-- skynet will call this function when the thread which create handle(traceid) end, and pass the time (ti)
	print("TRACE",trace_cache[handle],ti)
	trace_cache[handle] = nil
end)

local function f (i)
	-- create a trace object
	local traceid = skynet.trace()
	-- name traceid in trace_cache[]
	trace_cache[traceid] = "thread " .. i

	for i=1,i do
		skynet.sleep(i)
	end
end

skynet.start(function()
	for i = 1 , 100 do
		skynet.fork(f, i)
	end
end)


