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

return core