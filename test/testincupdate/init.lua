local Skynet = require 'skynet'
require 'skynet.manager'
local Service = require "skynet.service"
local Sharetable = require "skynet.sharetable"
local SC = require 'skynet.sharetable.core'
local ULock = require 'update_lock'


local function CloneTb(T)
    local out = {}
    for k, v in pairs(T) do
        out[k] = v
    end
    return out
end

local Cmd = {}
Cmd.register_services = {}

local function calc_diff(oval, nval, keys, gc_vals, none_gc_vals, new_keys)
    local kl = #keys + 1
    for k, v in pairs(nval) do
        keys[kl] = k
        local ov = oval[k]
        local nt = type(v)
        local ot = type(ov)
        if nt == ot and nt == 'table' then
            calc_diff(ov, v, keys, gc_vals, none_gc_vals, new_keys)
        elseif ov ~= v then
            local copy_keys = CloneTb(keys)
            if ot == 'nil' then
                new_keys[copy_keys] = v
            end
            if nt == 'table' or nt == 'string' or nt == 'function' or ot == 'nil' then
                gc_vals[copy_keys] = v
            else
                none_gc_vals[copy_keys] = v
            end
        end
    end
    if keys[kl] then
        table.remove(keys)
    end
end

local function find_del(oval, nval, keys, del_keys)
    local kl = #keys + 1
    for k, v in pairs(oval) do
        keys[kl] = k
        local nv = nval[k]
        local ot = type(v)
        local nt = type(nv)
        if nt == ot and nt == 'table' then
            find_del(v, nv, keys, del_keys)
        elseif nv == nil then
            local copy_keys = CloneTb(keys)
            table.insert(del_keys, copy_keys)
        end
    end
    local mt = getmetatable(oval)
    if mt then
        for k, v in pairs(mt.__index) do
            keys[kl] = k
            local nv = nval[k]
            local ot = type(v)
            local nt = type(nv)
            if nt == ot and nt == 'table' then
                find_del(v, nv, keys, del_keys)
            elseif nv == nil then
                local copy_keys = CloneTb(keys)
                table.insert(del_keys, copy_keys)
            end
        end
    end
    if keys[kl] then
        table.remove(keys)
    end
end

--[[
    name: 资源名
    old_res: 旧资源
    inc_old_res: 用于找出从旧资源中删除的字段（可能只是一小部分的old_res）
    new_res: 新资源（修改到的节点）
    reserve_stack: 持有name资源的虚拟机的初始函数栈需要保留的栈大小
    no_lock: 是否需要锁skynet线程
--]]
function Cmd.inc_update(name, old_res, inc_old_res, new_res, reserve_stack, no_lock)
    local keys = {}
    local gc_vals = {}
    local none_gc_vals = {}
    local new_keys = {}
    calc_diff(old_res, new_res, keys, gc_vals, none_gc_vals, new_keys)

    keys = {}
    local del_keys = {}
    find_del(inc_old_res, new_res, keys, del_keys)

    local root = {}
    local pointer = root
    local metatables = {}
    for ks, val in pairs(gc_vals) do
        local old_parent = old_res
        local klen = #ks
        for i=1, klen - 1 do
            local k = ks[i]
            old_parent = old_parent[k]
            if not pointer[k] then
                pointer[k] = {}
            end
            pointer = pointer[k]
        end
        if new_keys[ks] then
            if not pointer['__new_elements__'] then
                local meta_keys = {}
                for i = 1, klen - 1 do
                    meta_keys[i] = ks[i]
                end
                table.insert(metatables, meta_keys)
                local idx_tbl = { [ks[klen]] = val }
                local old_mt = getmetatable(old_parent)
                if old_mt then
                    -- v是table且含有metatable，调用skynet.pack会有问题
                    for k, v in pairs(old_mt.__index) do
                        idx_tbl[k] = true -- 先占个位
                    end
                end
                pointer['__new_elements__'] = { __index = idx_tbl, __pairs = true, __len = true }
            else
                pointer['__new_elements__'].__index[ks[klen]] = val
            end
        else
            pointer[ks[klen]] = val
        end
        pointer = root
    end
    
    local sroot = Sharetable.inc_update(name, root, reserve_stack)
    -- set __index and move old_mt elem to new mt
    for _, ks in ipairs(metatables) do
        local pointer, old_parent = sroot, old_res
        local klen = #ks
        for i=1, klen do
            local k = ks[i]
            pointer = pointer[k]
            old_parent = old_parent[k]
        end
        local old_mt = getmetatable(old_parent)
        if old_mt then
            local new_mt_idx = pointer['__new_elements__'].__index
            for k, v in pairs(old_mt.__index) do
                SC.set_table(new_mt_idx, k, v)
            end
        end
    end

    -- do the update
    if not no_lock then
        if not ULock.lock(Cmd.register_services) then
            return false
        end
    end
    for _, ks in ipairs(metatables) do -- 新增的值放到metatable里
        local old_parent, new_parent = old_res, sroot
        local klen = #ks
        for i = 1, klen do
            local k = ks[i]
            old_parent = old_parent[k]
            new_parent = new_parent[k]
        end
        SC.set_metatable(old_parent, new_parent['__new_elements__'])
    end
    for ks in pairs(gc_vals) do
        if next(ks) and not new_keys[ks] then
            local new_parent, old_parent = sroot, old_res
            for i = 1, #ks - 1 do
                new_parent = new_parent[ks[i]]
                old_parent = old_parent[ks[i]]
            end
            local key = ks[#ks]
            if rawget(old_parent, key) then
                SC.set_table(old_parent, key, new_parent[key])
            end
        end
    end
    for ks in pairs(none_gc_vals) do
        if next(ks) then
            local new_parent, old_parent = new_res, old_res
            for i = 1, #ks - 1 do
                new_parent = new_parent[ks[i]]
                old_parent = old_parent[ks[i]]
            end
            local key = ks[#ks]
            if rawget(old_parent, key) then
                SC.set_table(old_parent, key, new_parent[key])
            end
        end
    end
    -- delete keys
    for _, ks in ipairs(del_keys) do
        if next(ks) then
            local old_parent = old_res
            for i = 1, #ks - 1 do
                old_parent = old_parent[ks[i]]
            end
            local key = ks[#ks]
            if rawget(old_parent, key) then
                SC.set_table(old_parent, key, nil)
            else
                local mt = getmetatable(old_parent)
                if mt and rawget(mt.__index, key) then
                    SC.set_table(mt.__index, key, nil)
                end
            end
        end
    end
    if not no_lock then
        ULock.unlock()
    end
    Skynet.fork(function() collectgarbage() end)
    return true
end

function Cmd.register(addr)
    addr = Skynet.address(addr)
    print('update_slave.register: ', addr)
    Cmd.register_services[addr] = true
end

------------- test case ---------------
--[[
    1000次增量更新操作，send到的service不断地取数据
    inc_update最后的参数 true: 不锁线程 false: 锁线程
--]]
function Cmd.test_case1()
    print('test_case1.begin')
    Sharetable.loadtable('test_case1', { { 1 }, { 1 } })

    local l = Skynet.call(".launcher", "lua", "LIST")
    local services = {}
    local selfaddr = Skynet.address(Skynet.self())
    for addr in pairs(l) do
        if addr ~= selfaddr then
            services[addr] = true
        end
    end
    for addr in pairs(services) do
        if Cmd.register_services[addr] then
            print('send.test_case1: ', addr, selfaddr)
            Skynet.send(addr, 'update', 'test_case1')
        end
    end
    local tbl = Sharetable.query('test_case1')
    Cmd.case1_confirm = { {}, {} }
    local case1_confirm = Cmd.case1_confirm
    local mt1, mt2 = getmetatable(tbl[1]), getmetatable(tbl[2])
    case1_confirm[1][1], case1_confirm[2][1] = tostring(mt1), tostring(mt2)
    for i = 2, 1000 do
        Cmd.inc_update('test_case1', tbl, tbl, { { [i] = i }, { [i] = i } }, 20, true)
        mt1, mt2 = getmetatable(tbl[1]), getmetatable(tbl[2])
        case1_confirm[1][i], case1_confirm[2][i] = tostring(mt1), tostring(mt2)
    end
    print('test_case1.end')
end

function Cmd.test_case1_confirm(addr, result)
    print('test_case1_confirm: ', addr)
    local case1_confirm = Cmd.case1_confirm
    local r1 = result[1]
    local r2 = result[2]
    for k, v in pairs(r1) do
        local cv = case1_confirm[1][k]
        assert(cv == v, string.format('metatable not same 1: %s %s %s', k, v, cv))
    end
    for k, v in pairs(r2) do
        local cv = case1_confirm[2][k]
        assert(cv == v, string.format('metatable not same 2: %s %s %s', k, v, cv))
    end
end

local function spinlock_service()
    require 'update_slave'
end

Skynet.PTYPE_INCUPDATE = 13
Skynet.register_protocol {
    name = 'update',
    id = Skynet.PTYPE_INCUPDATE,
    unpack = Skynet.unpack,
    pack = Skynet.pack,
    dispatch = function(session, address, cmd_name, ...)
        print('default dispatch')
    end
}

Skynet.start(function ()
    Skynet.dispatch('lua', function(session, address, cmd, ...)
        local f = assert(Cmd[cmd], cmd)
        f(address, ...)
    end)
    Skynet.register('.test_inc_update')

    ULock.build_lock()

    -- 起 threads * 2 个测试service(在一般的上层服务中，都会处理update消息的dispatch，数量上会多得多，命中率会更大)
    local threads = math.tointeger(Skynet.getenv('thread')) * 2
    for i = 1, threads do
        Service.new('spinlock_service' .. i, spinlock_service)
    end

    Cmd.test_case1()
end)
