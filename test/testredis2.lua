local skynet = require "skynet"
local redis  = require "redis"

local db

function add1(key, count)
    local t = {}
    for i = 1, count do
        t[2*i -1] = "key" ..i
        t[2*i] = "value" .. i
    end
    db:hmset(key, table.unpack(t))
end

function add2(key, count)
    local t = {}
    for i = 1, count do
        t[2*i -1] = "key" ..i
        t[2*i] = "value" .. i
    end
    table.insert(t, 1, key)
    db:hmset(t)
end

function __init__()
    db = redis.connect {
        host = "127.0.0.1",
        port = 6300,
        db   = 0,
        auth = "foobared"
    }
    print("dbsize:", db:dbsize())
    local ok, msg = xpcall(add1, debug.traceback, "test1", 250000)
    if not ok then
        print("add1 failed", msg)
    else
        print("add1 succeed")

    end

    local ok, msg = xpcall(add2, debug.traceback, "test2", 250000)
    if not ok then
        print("add2 failed", msg)
    else
        print("add2 succeed")
    end
    print("dbsize:", db:dbsize())

    print("redistest launched")
end

skynet.start(__init__)

