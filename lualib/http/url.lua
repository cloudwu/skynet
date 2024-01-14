local url = {}

local function decode_func(c)
	return string.char(tonumber(c, 16))
end

local function decode(str)
	local str = str:gsub('+', ' ')
	return str:gsub("%%(..)", decode_func)
end

function url.parse(u)
	local path,query = u:match "([^?]*)%??(.*)"
	if path then
		path = decode(path)
	end
	return path, query
end

function url.parse_query(q)
	local r = {}
	for k,v in q:gmatch "(.-)=([^&]*)&?" do
		local dk, dv = decode(k), decode(v)
		local oldv = r[dk]
		if oldv then
			if type(oldv) ~= "table" then
				r[dk] = {oldv, dv}
			else
				oldv[#oldv+1] = dv
			end
		else
			r[dk] = dv
		end
	end
	return r
end

return url
