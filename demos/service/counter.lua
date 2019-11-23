local skynet = require "skynet"

local id = ...

local counter = 0
skynet.start(function()
    skynet.error("start counter service: ", id)

    skynet.dispatch("lua", function(session, address, cmd, ...)
        print(string.format("counter %d recv: %s %s %s", id, session, address, cmd), ...)
        if cmd == "active" then
            counter = counter + 1
            return
        end

        if cmd == "current_count" then
            skynet.retpack(counter)
            return
        end

        error(string.format("Unknown command %s", tostring(cmd)))
    end)
end)
