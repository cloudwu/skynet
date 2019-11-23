local skynet = require "skynet"
require "skynet.manager"	-- import skynet.register

local db = {}
skynet.start(function()
    skynet.dispatch("lua", function(session, address, cmd, key, value)
        if cmd == "set" then
            local last = db[key]
            db[key] = value
            return skynet.retpack(last)
        end

        if cmd == "get" then
            skynet.retpack(db[key])
            return
        end

        error(string.format("Unknown command %s", tostring(cmd)))
    end)

    skynet.register("MyDB")     -- register(name) 给当前服务起一个字符串名
end)
