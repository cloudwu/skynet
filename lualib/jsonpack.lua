local cjson = require "cjson"

local jsonpack = {}

function jsonpack.pack(session, v)
	return string.format("%d+%s", session, cjson.encode(v))
end

function jsonpack.response(session, v)
	return string.format("%d-%s",session, cjson.encode(v))
end

function jsonpack.unpack(msg)
	local session,t,str = string.match(msg, "(%d+)(.)(.*)")
	assert(t == '+')
	return tonumber(session) , cjson.decode(str)
end

return jsonpack