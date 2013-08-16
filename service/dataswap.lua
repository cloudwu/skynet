local skynet = require "skynet"
local dataswap = require "dataswap.c"

skynet.register_protocol {
	name = "client",
	id = 3,
	pack = function(...) return ... end,
	unpack = function(...) return ... end
 }

skynet.start(function()
	skynet.dispatch(
       "client", 
       function(session, address, message, sz)
          skynet.ret(dataswap.swap(message), sz)
       end)
	skynet.register "DATASWAP"
end)