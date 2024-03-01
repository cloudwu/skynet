local Skynet = require 'skynet'
local Sharetable = require "skynet.sharetable"
local SC = require 'skynet.sharetable.core'

local M = {}

M.id_num = 0

function M.build_lock(os_threadset)
    local update_lock = {
        state = false,
        id = 0,
    }
    Sharetable.loadtable('update_lock', update_lock)
    M.ulock = Sharetable.query('update_lock')
    M.lock_ptr = Sharetable.query_ptr('update_lock')
    M.counter_ptr = Sharetable.new_thread_counter('update_lock')
    print('build update lock success')
end

function M.lock(register_services)
    local ulock = M.ulock
    if ulock.state then
        print('locking now')
        return false
    end
    local l = Skynet.call(".launcher", "lua", "LIST")
    local services = {}
    local selfaddr = Skynet.address(Skynet.self())
    for addr in pairs(l) do
        if addr ~= selfaddr then
            services[addr] = true
        end
    end
    M.id_num = M.id_num + 1

    local threads = math.tointeger(Skynet.getenv('thread'))
    SC.reset_thread_cnt(M.counter_ptr)
    SC.set_table(ulock, 'state', true)
    SC.set_table(ulock, 'id', M.id_num)
    for addr in pairs(services) do
        if register_services[addr] then
            Skynet.send(addr, 'update', 'spinlock', ulock.id, M.lock_ptr, M.counter_ptr)
        end
    end
    local lock_cnt
    local func_now = Skynet.now
    local lock_point = func_now()
    while true do
        lock_cnt = SC.get_thread_cnt(M.counter_ptr)
        if lock_cnt == threads - 1 then
            break
        end
        if func_now() - lock_point > 30 then
            print('lock timeout: %s', lock_cnt)
            return true
        end
    end
    print('update_loc suc: ', SC.get_thread_cnt(M.counter_ptr), threads, ulock.id, func_now() - lock_point)
    return true
end

function M.unlock()
    SC.set_table(M.ulock, 'state', false)
    print('update_lock.unlock: ', M.ulock.id)
end

return M
