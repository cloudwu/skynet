local skynet = require "skynet"
skynet.start(function()
    print(skynet.starttime())
    print(skynet.now())
end)
