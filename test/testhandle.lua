local skynet = require "skynet"
require "skynet.manager"

local mod = ...
if mod == "slave" then

skynet.start(function()
    skynet.error("addr:", skynet.self())
end)

else

skynet.start(function()
	skynet.newservice("debug_console",8000)
	skynet.error("master addr:", skynet.self())

    skynet.newservice("testhandle", "slave")
    skynet.newservice("testhandle", "slave")
    skynet.newservice("testhandle", "slave")
    skynet.newservice("testhandle", "slave")
    skynet.newservice("testhandle", "slave")
    skynet.newservice("testhandle", "slave")

    while true do
        local addr = skynet.newservice("testhandle", "slave")
        skynet.kill(addr)
        if addr > 0xfffff0 then
            break
        end
    end
end)

end
