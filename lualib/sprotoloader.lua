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
	return sproto.new(bin,sz)
end

return loader

