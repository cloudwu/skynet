local parser = require "sprotoparser"
local core = require "sproto.core"
local sproto = require "sproto"

local loader = {}

function loader.register(filename, index)
	local f = assert(io.open(filename), "Can't open sproto file")
	local data = f:read "a"
	f:close()
	local bin = parser.parse(data)
	core.saveproto(bin, index)
end

loader.save = core.saveproto

function loader.load(index)
	local bin, sz = core.loadproto(index)
	--  no __gc in metatable
	return sproto.new(bin,sz, true)
end

return loader

