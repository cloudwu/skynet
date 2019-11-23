local skynet = require "skynet"
local socket = require "skynet.socket"

local id = ...

local function echo(id)
    -- 调用 socket.start(id) 之后，才可以收到这个 socket 上的数据
    socket.start(id)

    while true do
        local str = socket.read(id)
        if str then
            socket.write(id, str)
        else
            socket.close(id)
            print("close id", id)
            return
        end
    end
end


skynet.start(function()
    local id = tonumber(id)
    skynet.fork(function()
        echo(id)
        skynet.exit()
    end)
end)
