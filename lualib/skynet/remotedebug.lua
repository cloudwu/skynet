local skynet = require "skynet"
local debugchannel = require "debugchannel"
local socket = require "socket"
local injectrun = require "skynet.injectcode"
local table = table
local debug = debug
local coroutine = coroutine
local sethook = debugchannel.sethook


local M = {}

local HOOK_FUNC = "raw_dispatch_message"
local raw_dispatcher
local print = _G.print
local skynet_suspend
local prompt
local newline

local function change_prompt(s)
	newline = true
	prompt = s
end

local function replace_upvalue(func, uvname, value)
	local i = 1
	while true do
		local name, uv = debug.getupvalue(func, i)
		if name == nil then
			break
		end
		if name == uvname then
			if value then
				debug.setupvalue(func, i, value)
			end
			return uv
		end
		i = i + 1
	end
end

local function remove_hook(dispatcher)
	assert(raw_dispatcher, "Not in debug mode")
	replace_upvalue(dispatcher, HOOK_FUNC, raw_dispatcher)
	raw_dispatcher = nil
	print = _G.print

	skynet.error "Leave debug mode"
end

local function gen_print(fd)
	-- redirect print to socket fd
	return function(...)
		local tmp = table.pack(...)
		for i=1,tmp.n do
			tmp[i] = tostring(tmp[i])
		end
		table.insert(tmp, "\n")
		socket.write(fd, table.concat(tmp, "\t"))
	end
end

local function run_exp(ok, ...)
	if ok then
		print(...)
	end
	return ok
end

local function run_cmd(cmd, env, co, level)
	if not run_exp(injectrun("return "..cmd, co, level, env)) then
		print(select(2, injectrun(cmd,co, level,env)))
	end
end

local ctx_skynet = debug.getinfo(skynet.start,"S").short_src	-- skip when enter this source file
local ctx_term = debug.getinfo(run_cmd, "S").short_src	-- term when get here
local ctx_active = {}

local linehook
local function skip_hook(mode)
	local co = coroutine.running()
	local ctx = ctx_active[co]
	if mode == "return" then
		ctx.level = ctx.level - 1
		if ctx.level == 0 then
			ctx.needupdate = true
			sethook(linehook, "crl")
		end
	else
		ctx.level = ctx.level + 1
	end
end

function linehook(mode, line)
	local co = coroutine.running()
	local ctx = ctx_active[co]
	if mode ~= "line" then
		ctx.needupdate = true
		if mode ~= "return" then
			if ctx.next_mode or debug.getinfo(2,"S").short_src == ctx_skynet then
				ctx.level = 1
				sethook(skip_hook, "cr")
			end
		end
	else
		if ctx.needupdate then
			ctx.needupdate = false
			ctx.filename = debug.getinfo(2, "S").short_src
			if ctx.filename == ctx_term then
				ctx_active[co] = nil
				sethook()
				change_prompt(string.format(":%08x>", skynet.self()))
				return
			end
		end
		change_prompt(string.format("%s(%d)>",ctx.filename, line))
		return true	-- yield
	end
end

local function add_watch_hook()
	local co = coroutine.running()
	local ctx = {}
	ctx_active[co] = ctx
	local level = 1
	sethook(function(mode)
		if mode == "return" then
			level = level - 1
		else
			level = level + 1
			if level == 0 then
				ctx.needupdate = true
				sethook(linehook, "crl")
			end
		end
	end, "cr")
end

local function watch_proto(protoname, cond)
	local proto = assert(replace_upvalue(skynet.register_protocol, "proto"), "Can't find proto table")
	local p = proto[protoname]
	local dispatch = p.dispatch_origin or p.dispatch
	if p == nil or dispatch == nil then
		return "No " .. protoname
	end
	p.dispatch_origin = dispatch
	p.dispatch = function(...)
		if not cond or cond(...) then
			p.dispatch = dispatch	-- restore origin dispatch function
			add_watch_hook()
		end
		dispatch(...)
	end
end

local function remove_watch()
	for co in pairs(ctx_active) do
		sethook(co)
	end
	ctx_active = {}
end

local dbgcmd = {}

function dbgcmd.s(co)
	local ctx = ctx_active[co]
	ctx.next_mode = false
	skynet_suspend(co, coroutine.resume(co))
end

function dbgcmd.n(co)
	local ctx = ctx_active[co]
	ctx.next_mode = true
	skynet_suspend(co, coroutine.resume(co))
end

function dbgcmd.c(co)
	sethook(co)
	ctx_active[co] = nil
	change_prompt(string.format(":%08x>", skynet.self()))
	skynet_suspend(co, coroutine.resume(co))
end

local function hook_dispatch(dispatcher, resp, fd, channel)
	change_prompt(string.format(":%08x>", skynet.self()))

	print = gen_print(fd)
	local env = {
		print = print,
		watch = watch_proto
	}

	local watch_env = {
		print = print
	}

	local function watch_cmd(cmd)
		local co = next(ctx_active)
		watch_env._CO = co
		if dbgcmd[cmd] then
			dbgcmd[cmd](co)
		else
			run_cmd(cmd, watch_env, co, 0)
		end
	end

	local function debug_hook()
		while true do
			if newline then
				socket.write(fd, prompt)
				newline = false
			end
			local cmd = channel:read()
			if cmd then
				if cmd == "cont" then
					-- leave debug mode
					break
				end
				if cmd ~= "" then
					if next(ctx_active) then
						watch_cmd(cmd)
					else
						run_cmd(cmd, env, coroutine.running(),2)
					end
				end
				newline = true
			else
				-- no input
				return
			end
		end
		-- exit debug mode
		remove_watch()
		remove_hook(dispatcher)
		resp(true)
	end

	local func
	local function hook(...)
		debug_hook()
		return func(...)
	end
	func = replace_upvalue(dispatcher, HOOK_FUNC, hook)
	if func then
		local function idle()
			skynet.timeout(10,idle)	-- idle every 0.1s
		end
		skynet.timeout(0, idle)
	end
	return func
end

function M.start(import, fd, handle)
	local dispatcher = import.dispatch
	skynet_suspend = import.suspend
	assert(raw_dispatcher == nil, "Already in debug mode")
	skynet.error "Enter debug mode"
	local channel = debugchannel.connect(handle)
	raw_dispatcher = hook_dispatch(dispatcher, skynet.response(), fd, channel)
end

return M
