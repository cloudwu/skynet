if _VERSION == "Lua 5.2" then
	return
end

local assert = assert

assert(_VERSION == "Lua 5.1")

local setfenv = setfenv
local loadstring = loadstring
local load = load
local io = io

function _G.load(ld, chunkname, mode, env)
	local f,err
	if type(ld) == "string" then
		f,err = loadstring(ld, chunkname)
	else
		f,err = load(ld, chunkname)
	end
	if f == nil then
        assert(f, err)
		return f, err
	end
	if env then
		setfenv(f, env)
	end

	return f,err
end

function _G.loadfile(filename, mode, env)
	local f = io.open(filename, "rb")
	assert(f, filename)
	local source = f:read "*a"
	f:close()
	return _G.load(source, "@" .. filename, mode, env)
end

_G.loadstring = _G.load
