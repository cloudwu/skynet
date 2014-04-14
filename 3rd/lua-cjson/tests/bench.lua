#!/usr/bin/env lua

-- This benchmark script measures wall clock time and should be
-- run on an unloaded system.
--
-- Your Mileage May Vary.
--
-- Mark Pulford <mark@kyne.com.au>

local json_module = os.getenv("JSON_MODULE") or "cjson"

require "socket"
local json = require(json_module)
local util = require "cjson.util"

local function find_func(mod, funcnames)
    for _, v in ipairs(funcnames) do
        if mod[v] then
            return mod[v]
        end
    end

    return nil
end

local json_encode = find_func(json, { "encode", "Encode", "to_string", "stringify", "json" })
local json_decode = find_func(json, { "decode", "Decode", "to_value", "parse" })

local function average(t)
    local total = 0
    for _, v in ipairs(t) do
        total = total + v
    end
    return total / #t
end

function benchmark(tests, seconds, rep)
    local function bench(func, iter)
        -- Use socket.gettime() to measure microsecond resolution
        -- wall clock time.
        local t = socket.gettime()
        for i = 1, iter do
            func(i)
        end
        t = socket.gettime() - t

        -- Don't trust any results when the run lasted for less than a
        -- millisecond - return nil.
        if t < 0.001 then
            return nil
        end

        return (iter / t)
    end

    -- Roughly calculate the number of interations required
    -- to obtain a particular time period.
    local function calc_iter(func, seconds)
        local iter = 1
        local rate
        -- Warm up the bench function first.
        func()
        while not rate do
            rate = bench(func, iter)
            iter = iter * 10
        end
        return math.ceil(seconds * rate)
    end

    local test_results = {}
    for name, func in pairs(tests) do
        -- k(number), v(string)
        -- k(string), v(function)
        -- k(number), v(function)
        if type(func) == "string" then
            name = func
            func = _G[name]
        end

        local iter = calc_iter(func, seconds)

        local result = {}
        for i = 1, rep do
            result[i] = bench(func, iter)
        end

        -- Remove the slowest half (round down) of the result set
        table.sort(result)
        for i = 1, math.floor(#result / 2) do
            table.remove(result, 1)
        end

        test_results[name] = average(result)
    end

    return test_results
end

function bench_file(filename)
    local data_json = util.file_load(filename)
    local data_obj = json_decode(data_json)

    local function test_encode()
        json_encode(data_obj)
    end
    local function test_decode()
        json_decode(data_json)
    end

    local tests = {}
    if json_encode then tests.encode = test_encode end
    if json_decode then tests.decode = test_decode end

    return benchmark(tests, 0.1, 5)
end

-- Optionally load any custom configuration required for this module
local success, data = pcall(util.file_load, ("bench-%s.lua"):format(json_module))
if success then
    util.run_script(data, _G)
    configure(json)
end

for i = 1, #arg do
    local results = bench_file(arg[i])
    for k, v in pairs(results) do
        print(("%s\t%s\t%d"):format(arg[i], k, v))
    end
end

-- vi:ai et sw=4 ts=4:
