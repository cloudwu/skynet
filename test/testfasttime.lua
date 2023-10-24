local skynet = require "skynet"

skynet.start(function()
    local cur_time = math.floor(skynet.time())
    skynet.error("testfasttime starttime:",os.date("%Y%m%d-%H:%M:%S",cur_time))
    skynet.fork(function()
        while true do
            skynet.sleep(6000)
            skynet.error("testfasttime curtime:",os.date("%Y%m%d-%H:%M:%S",math.floor(skynet.time())))
        end
    end)
end)