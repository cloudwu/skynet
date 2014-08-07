local skynet = require "skynet"
local c = require "skynet.c"
local snax_interface = require "snax.interface"
local profile = require "profile"
local snax = require "snax"

local snax_name = tostring(...)
local func, pattern = snax_interface(snax_name, _ENV)
local snax_path = pattern:sub(1,pattern:find("?", 1, true)-1) .. snax_name ..  "/"
package.path = snax_path .. "?.lua;" .. package.path

SERVICE_NAME = snax_name
SERVICE_PATH = snax_path

local mode
local thread_id
local message_queue = {}
local init = false
local profile_table = {}

local function update_stat(name, ti)
	local t = profile_table[name]
	if t == nil then
		t = { count = 0,  time = 0 }
		profile_table[name] = t
	end
	t.count = t.count + 1
	t.time = t.time + ti
end

local traceback = debug.traceback

local function do_func(f, msg)
	return xpcall(f, traceback, table.unpack(msg))
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
				if method[2] == "accept" then
					-- no return
					profile.start()
					local ok, data = xpcall(f, traceback, table.unpack(msg))
					local ti = profile.stop()
					update_stat(method[3], ti)
					if not ok then
						print(string.format("Error on [:%x] to [:%x] %s", msg.source, skynet.self(), tostring(data)))
					end
				else
					profile.start()
					local ok, data, size = xpcall(dispatch, traceback, f, table.unpack(msg))
					local ti = profile.stop()
					update_stat(method[3], ti)
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

local function return_f(f, ...)
	return skynet.ret(skynet.pack(f(...)))
end

local function timing( method, ... )
	local err, msg
	profile.start()
	if method[2] == "accept" then
		-- no return
		err,msg = xpcall(method[4], traceback, ...)
	else
		err,msg = xpcall(return_f, traceback, method[4], ...)
	end
	local ti = profile.stop()
	update_stat(method[3], ti)
	assert(err,msg)
end

skynet.start(function()
	skynet.dispatch("snax", function ( session , source , id, ...)
		local method = func[id]

		if method[2] == "system" then
			local command = method[3]
			if command == "hotfix" then
				local hotfix = require "snax.hotfix"
				skynet.ret(skynet.pack(hotfix(func, ...)))
			elseif command == "init" then
				assert(not init, "Already init")
				local initfunc = method[4] or function() end
				mode = initfunc(...)
				if mode == "queue" then
					skynet.fork(message_dispatch)
				end
				skynet.ret()
				skynet.info_func(function()
					return profile_table
				end)
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
			else
				timing(method, ...)
			end
		end
	end)
end)
