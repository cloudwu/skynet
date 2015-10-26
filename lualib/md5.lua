----------------------------------------------------------------------------
-- Modify version from https://github.com/keplerproject/md5
----------------------------------------------------------------------------

local core = require "md5.core"

----------------------------------------------------------------------------
-- @param k String with original message.
-- @return String with the md5 hash value converted to hexadecimal digits

function core.sumhexa (k)
	k = core.sum(k)
	return (string.gsub(k, ".", function (c)
		   return string.format("%02x", string.byte(c))
		 end))
end

function core.hmac_md5(data,key)
	if #key>64 then
		key=core.sum(key)
		key=string.sub(key,1,16)
	end

	local b=table.pack(string.byte(key,1,#key))
	local ipad_s=""
	local opad_s=""
	for i=1,64 do
		ipad_s=ipad_s..string.char((b[i] or 0)~0x36)
		opad_s=opad_s..string.char((b[i] or 0)~0x5c)
	end
	local istr=core.sum(ipad_s..data)
	local ostr=core.sumhexa(opad_s..istr)
	return ostr
end

return core
