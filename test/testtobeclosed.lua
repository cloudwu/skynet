local skynet = require "skynet"

local function new_test(name)
    return setmetatable({}, { __close = function(...)
        skynet.error(...)
    end, __name = "closemeta:" .. name})
end

local i = 0
skynet.dispatch("lua", function()
    i = i + 1
    if i==2 then
        local c<close> = new_test("dispatch_error")
        error("dispatch_error")
    else
        local c<close> = new_test("dispatch_wait")
        skynet.wait()
    end
end)

skynet.start(function()
    local c<close> = new_test("skynet.exit")
    skynet.fork(function()
        local a<close> = new_test("stack_raise_error")
        error("raise error")
    end)
    skynet.fork(function()
        local a<close> = new_test("session_id_coroutine_wait")
        skynet.wait()
    end)
    skynet.fork(function()
        local a<close> = new_test("session_id_coroutine_call")
        skynet.call(skynet.self(), "lua")
    end)
    skynet.fork(function()
        skynet.call(skynet.self(), "lua")
    end)
    skynet.sleep(100)
    skynet.fork(function()
        local a<close> = new_test("no_running")
        skynet.wait()
    end)
    skynet.exit()
end)

--[[
testtobeclosed
]]