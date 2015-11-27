local parser = require "sprotoparser"
local core = require "sproto.core"
local sproto = require "sproto"

local loader = {}

function loader.register(filename, index, isbin)
	local f = assert(io.open(filename), "Can't open sproto file")
	local data = f:read "a"
	f:close()

	local bin = isbin and data or parser.parse(data)
	loader.save(bin, index)
end

function loader.save(bin, index)
	local sp = core.newproto(bin)
	core.saveproto(sp, index)
end

function loader.load(index)
	local sp = core.loadproto(index)
	--  no __gc in metatable
	return sproto.sharenew(sp)
end

return loader
