local cookie

local function make_ld(ld)
	local load_cookie
	return function()
		if load_cookie then
			return ld()
		else
			load_cookie = true
			return cookie
		end
	end
end

if _VERSION == "Lua 5.2" then

cookie = "local function __setenv() end;"

local load = load

function _G.load(ld, chunkname, mode, env)
	local f
	if type(ld) == "string" then
		f = load(cookie .. ld, chunkname, mode, env)
	else
		f = load(make_ld(ld), chunkname)
	end

	return f
end

function _G.loadfile(filename, mode, env)
	local f = io.open(filename, "rb")
	assert(f, filename)
	local source = f:read "*a"
	f:close()
	return _G.load(source, "@" .. filename, mode, env)
end

_G.loadstring = _G.load

	return
end

local assert = assert

assert(_VERSION == "Lua 5.1")

local setfenv = setfenv
local getfenv = getfenv
local loadstring = loadstring
local load = load
local io = io

local function __setenv(env)
	setfenv(2,env)
end

cookie = "local _ENV,__setenv = __getenv();"

function _G.load(ld, chunkname, mode, env)
	local f
	if type(ld) == "string" then
		f = loadstring(cookie .. ld, chunkname)
	else
		f = load(make_ld(ld), chunkname)
	end
	env = env or getfenv(1)
	setfenv(f, { __getenv = function()
		setfenv(2, env)
		return env, __setenv
	end })

	return f
end

function _G.loadfile(filename, mode, env)
	local f = io.open(filename, "rb")
	assert(f, filename)
	local source = f:read "*a"
	f:close()
	return _G.load(source, "@" .. filename, mode, env)
end

_G.loadstring = _G.load
_G._ENV = _G
