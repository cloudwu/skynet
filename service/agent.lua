local skynet = require "skynet"
local client = ...

local dataswap_example = true

if not dataswap_example then
   print("---- SIMPLEDB agent ----")
   skynet.register_protocol {
      name = "client",
      id = 3,
      pack = function(...) return ... end,
      unpack = skynet.tostring,
      dispatch = function (session, address, text)
                    -- It's client, there is no session
                    skynet.send("LOG", "text", "client message :" .. text)
                    local result = skynet.call("SIMPLEDB", "text", text)
                    skynet.ret(result)
                 end
   }

   skynet.start(function()
                   skynet.send(client,"text","Welcome to skynet")
                end)
else
   print("---- DATASWAP agent ----")
   skynet.register_protocol {
      name = "client",
      id = 3,
      pack = function(...) return ... end,
      unpack = function(...) return ... end,
      dispatch = 
         function (session, address, msg, sz)
            -- It's client, there is no session
            skynet.send("LOG", "text", "dataswap message type :", type(msg), sz)
            local result = skynet.rawcall("DATASWAP", "client", msg, sz)
            skynet.ret(result, sz)
         end
   }

   skynet.start(
      function()
         skynet.send(client,"text","Welcome to skynet")
      end)
end
