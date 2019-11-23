local skynet = require "skynet"
local socket = require "skynet.socket"

local function accept(id)
    socket.start(id)
    socket.write(id, "Hello Skynet\n")
    skynet.newservice("agent", id)
    -- 注意：在新服务调用socket.start前，客户端如果发送数据，将会丢失
    -- 清除 socket id 在本服务内的数据结构，但并不关闭这个 socket
    socket.abandon(id)
end

skynet.start(function()
    local id = socket.listen("127.0.0.1", 8001)
    print("Listen socket :", "127.0.0.1", 8001)

    -- socket.start(id , accept) accept是一个函数
    -- 每当一个监听的id对应的socket上有连接接入的时候，都会调用accept函数
    socket.start(id , function(id, addr)
        print("connect from " .. addr .. " " .. id)
        -- you have choices :
        -- 1. skynet.newservice("testsocket", "agent", id)
        -- 2. skynet.fork(echo, id)
        -- 3. accept(id)
        accept(id)
    end)
end)
