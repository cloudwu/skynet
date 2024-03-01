local Skynet = require 'skynet'
local Sharetable = require 'skynet.sharetable'
local SC = require 'skynet.sharetable.core'

local Cmd = {}
function Cmd.spinlock(addr, id, lock_ptr, counter_ptr)
    local my_addr = string.format('%08x',Skynet.self())
    print(string.format('The service [%s] receive lock', my_addr))
    local lock = SC.clone(lock_ptr) -- 传指针用clone接口转化
    if not lock then
        return
    end
    if lock.id ~= id then
        print('service lock change: ', my_addr, lock.id, id)
        return
    end
    if lock.state == false then
        print('service lock lost: ', my_addr)
        return
    end
    SC.inc_thread_cnt(counter_ptr)
    print('service get lock: ', my_addr, SC.get_thread_cnt(counter_ptr), id)
    while true do
        if lock.state == false then
            print('service free: ', my_addr, id)
            break
        end
    end
end

------------- test case ---------------

function Cmd.test_case1(addr)
    local selfaddr = Skynet.address(Skynet.self())
    print('receive.test_case1.begin: ', selfaddr, addr)
    local tbl = Sharetable.query('test_case1')
    local mt1, mt2 = { ['__index'] = {1} }, { ['__index'] = {1} }
    local set = { {}, {} }
    local result = { {}, {} }
    local same_cnt = 0
    local loop_cnt = 0
    local real_cnt1, real_cnt2 = 0, 0
    while true do
        local idx1, val1 = next(tbl[1])
        if not idx1 then
            mt1 = getmetatable(tbl[1])
            idx1, val1 = next(mt1.__index)
        end
        local idx2, val2 = next(tbl[2])
        if not idx2 then
            mt2 = getmetatable(tbl[2])
            idx2, val2 = next(mt2.__index)
        end

        Skynet.sleep(0)
        if not set[1][mt1] then
            set[1][mt1] = true
            if val1 ~= 1 then
                result[1][val1] = tostring(mt1)
            end
            real_cnt1 = real_cnt1 + 1
        end
        if not set[2][mt2] then
            set[2][mt2] = true
            if val2 ~= 1 then
                result[2][val2] = tostring(mt2)
            end
            real_cnt2 = real_cnt2 + 1
        end
        loop_cnt = loop_cnt + 1
        if val1 ~= val2 then
            print('test_case1 not same: ', selfaddr, val1, val2)
        end
        if val1 == 1000 and val1 == val2 then
            break
        end
    end
    Skynet.send(addr, 'lua', 'test_case1_confirm', result)
    print('receive.test_case1.end: ', selfaddr, real_cnt1, real_cnt2, loop_cnt)
end

Skynet.PTYPE_INCUPDATE = 13
Skynet.register_protocol {
    name = 'update',
    id = Skynet.PTYPE_INCUPDATE,
    unpack = Skynet.unpack,
    pack = Skynet.pack,
    dispatch = function(session, address, cmd_name, ...)
        local f = Cmd[cmd_name]
        assert(f, cmd_name)
        f(address, ...)
    end
}

Skynet.init(function()
    Skynet.send('.test_inc_update', 'lua', 'register')
end)
