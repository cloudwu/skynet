local skynet = require "skynet"
local c = require "skynet.c"
local snax_interface = require "snax_interface"

local func = snax_interface(tostring(...), _ENV)
local mode
local thread_id
local message_queue = {}
local init = false

local function do_func(f, msg)
	return pcall(f, table.unpack(msg))
end

local function dispatch(f, ...)
	return skynet.pack(f(...))
end

local function message_dispatch()
	while true do
		if #message_queue==0 then
			thread_id = coroutine.running()
			skynet.wait()
		else
			local msg = table.remove(message_queue,1)
			local method = msg.method
			local f = method[4]
			if f then
				if method[2] == "subscribe" then
					-- no return
					local ok, data = pcall(f, table.unpack(msg))
					if not ok then
						print(string.format("Error on [:%x] to [:%x] %s", msg.source, skynet.self(), tostring(data)))
					end
				else
					local ok, data, size = pcall(dispatch, f, table.unpack(msg))
					if ok then
						-- skynet.PTYPE_RESPONSE == 1
						c.send(msg.source, 1, msg.session, data, size)
						if method[2] == "system" then
							init = false
							skynet.exit()
							break
						end
					else
						-- Can't throw error, so print it directly
						print(string.format("Error on [:%x] to [:%x] %s", msg.source, skynet.self(), tostring(data)))
						c.send(msg.source, skynet.PTYPE_ERROR, msg.session, "")
					end
				end
			end
		end
	end
end

local function queue( session, source, method, ...)
	table.insert(message_queue, {session = session, source = source, method = method, ... })
	if thread_id then
		skynet.wakeup(thread_id)
		thread_id = nil
	end
end

skynet.start(function()
	skynet.dispatch("lua", function ( session , source , id, ...)
		local method = func[id]

		if method[2] == "system" then
			local command = method[3]
			if command == "hotfix" then
				local hotfix = require "snax_hotfix"
				skynet.ret(skynet.pack(hotfix(func, ...)))
			elseif command == "init" then
				assert(not init, "Already init")
				local initfunc = method[4] or function() end
				mode = initfunc(...)
				if mode == "queue" then
					skynet.fork(message_dispatch)
				end
				skynet.ret()
				init = true
			elseif mode == "queue" then
				queue( session, source, method , ...)
			else
				assert(init, "Never init")
				assert(command == "exit")
				local exitfunc = method[4] or function() end
				exitfunc(...)
				skynet.ret()
				init = false
				skynet.exit()
			end
		else
			assert(init, "Init first")
			if mode == "queue" then
				queue(session, source, method , ...)
			elseif method[2] == "subscribe" then
				-- no return
				method[4](...)
			else
				skynet.ret(skynet.pack(method[4](...)))
			end
		end
	end)
end)
