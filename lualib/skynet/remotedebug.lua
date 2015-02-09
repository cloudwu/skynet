local skynet = require "skynet"
local debugchannel = require "debugchannel"
local socket = require "socket"

local M = {}

local HOOK_FUNC = "raw_dispatch_message"
local raw_dispatcher

local function remove_hook(dispatcher)
	assert(raw_dispatcher, "Not in debug mode")
	for i=1,8 do
		local name, func = debug.getupvalue(dispatcher, i)
		if name == HOOK_FUNC then
			debug.setupvalue(dispatcher, i, raw_dispatcher)
			break
		end
	end
	raw_dispatcher = nil

	skynet.error "Leave debug mode"
end

local function idle()
	skynet.timeout(10,idle)	-- idle every 0.1s
end

local function hook_dispatch(dispatcher, resp, fd, channel)
	local address = skynet.self()
	local prompt = string.format(":%08x>", address)
	local newline = true

	local function print(...)
		local tmp = table.pack(...)
		for i=1,tmp.n do
			tmp[i] = tostring(tmp[i])
		end
		table.insert(tmp, "\n")
		socket.write(fd, table.concat(tmp, "\t"))
	end

	local message = {}
	local cond

	local function breakpoint(f)
		cond = f
	end

	local env = setmetatable({ skynet = skynet, print = print, hook = breakpoint, m = message }, { __index = _ENV })

	local function debug_hook(proto, msg, sz, session, source)
		message.proto = proto
		message.session = session
		message.address = source
		message.message = msg
		message.size = sz
		local sleep = nil
		while true do
			if newline then
				socket.write(fd, prompt)
				newline = false
			end
			local cmd = channel:read(sleep)
			if cmd then
				if cmd == "cont" then
					break
				end
				if cmd ~= "" then
					if sleep then
						if cmd == "c" then
							print "continue..."
							prompt = string.format(":%08x>", address)
							newline = true
							return
						end
					end

					local f = load("return "..cmd, "=(debug)", "t", env)
					if not f then
						local err
						f,err = load(cmd, "=(debug)", "t", env)
						if not f then
							socket.write(fd, err .. "\n")
						end
					end
					if f then
						print(select(2,pcall(f)))
					end
				end
				newline = true
			else
				-- no input
				if sleep == nil then
					if cond and cond(message) then
						-- cond break point
						prompt = "break>"
						sleep = 0.1
						print()
						newline = true
					else
						return
					end
				end
			end
		end
		-- exit debug mode
		remove_hook(dispatcher)
		resp(true)
	end

	for i=1,8 do
		local name, func = debug.getupvalue(dispatcher, i)
		if name == HOOK_FUNC then
			local function hook(...)
				debug_hook(...)
				return func(...)
			end
			debug.setupvalue(dispatcher, i, hook)
			skynet.timeout(0, idle)
			return func
		end
	end
end

function M.start(dispatcher, fd, handle)
	assert(raw_dispatcher == nil, "Already in debug mode")
	skynet.error "Enter debug mode"
	local channel = debugchannel.connect(handle)
	raw_dispatcher = hook_dispatch(dispatcher, skynet.response(), fd, channel)
end

return M
