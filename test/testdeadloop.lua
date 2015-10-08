local skynet = require "skynet"
local function dead_loop()
    while true do
        skynet.sleep(0)
    end
end

skynet.start(function()
    skynet.fork(dead_loop)
end)
