local skynet = require "skynet"

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	pack = function (...)
		local n = select ("#" , ...)
		if n == 0 then
			return ""
		elseif n == 1 then
			return tostring(...)
		else
			return table.concat({...}," ")
		end
	end,
	unpack = skynet.tostring
}

skynet.start(function()
	skynet.dispatch("text", function(session, address, text)
		print("[GLOBALLOG]", skynet.address(address),text)
	end)
	skynet.register "LOG"
end)
