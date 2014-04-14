local json = require "cjson"

-- Various common routines used by the Lua CJSON package
--
-- Mark Pulford <mark@kyne.com.au>

-- Determine with a Lua table can be treated as an array.
-- Explicitly returns "not an array" for very sparse arrays.
-- Returns:
-- -1   Not an array
-- 0    Empty table
-- >0   Highest index in the array
local function is_array(table)
    local max = 0
    local count = 0
    for k, v in pairs(table) do
        if type(k) == "number" then
            if k > max then max = k end
            count = count + 1
        else
            return -1
        end
    end
    if max > count * 2 then
        return -1
    end

    return max
end

local serialise_value

local function serialise_table(value, indent, depth)
    local spacing, spacing2, indent2
    if indent then
        spacing = "\n" .. indent
        spacing2 = spacing .. "  "
        indent2 = indent .. "  "
    else
        spacing, spacing2, indent2 = " ", " ", false
    end
    depth = depth + 1
    if depth > 50 then
        return "Cannot serialise any further: too many nested tables"
    end

    local max = is_array(value)

    local comma = false
    local fragment = { "{" .. spacing2 }
    if max > 0 then
        -- Serialise array
        for i = 1, max do
            if comma then
                table.insert(fragment, "," .. spacing2)
            end
            table.insert(fragment, serialise_value(value[i], indent2, depth))
            comma = true
        end
    elseif max < 0 then
        -- Serialise table
        for k, v in pairs(value) do
            if comma then
                table.insert(fragment, "," .. spacing2)
            end
            table.insert(fragment,
                ("[%s] = %s"):format(serialise_value(k, indent2, depth),
                                     serialise_value(v, indent2, depth)))
            comma = true
        end
    end
    table.insert(fragment, spacing .. "}")

    return table.concat(fragment)
end

function serialise_value(value, indent, depth)
    if indent == nil then indent = "" end
    if depth == nil then depth = 0 end

    if value == json.null then
        return "json.null"
    elseif type(value) == "string" then
        return ("%q"):format(value)
    elseif type(value) == "nil" or type(value) == "number" or
           type(value) == "boolean" then
        return tostring(value)
    elseif type(value) == "table" then
        return serialise_table(value, indent, depth)
    else
        return "\"<" .. type(value) .. ">\""
    end
end

local function file_load(filename)
    local file
    if filename == nil then
        file = io.stdin
    else
        local err
        file, err = io.open(filename, "rb")
        if file == nil then
            error(("Unable to read '%s': %s"):format(filename, err))
        end
    end
    local data = file:read("*a")

    if filename ~= nil then
        file:close()
    end

    if data == nil then
        error("Failed to read " .. filename)
    end

    return data
end

local function file_save(filename, data)
    local file
    if filename == nil then
        file = io.stdout
    else
        local err
        file, err = io.open(filename, "wb")
        if file == nil then
            error(("Unable to write '%s': %s"):format(filename, err))
        end
    end
    file:write(data)
    if filename ~= nil then
        file:close()
    end
end

local function compare_values(val1, val2)
    local type1 = type(val1)
    local type2 = type(val2)
    if type1 ~= type2 then
        return false
    end

    -- Check for NaN
    if type1 == "number" and val1 ~= val1 and val2 ~= val2 then
        return true
    end

    if type1 ~= "table" then
        return val1 == val2
    end

    -- check_keys stores all the keys that must be checked in val2
    local check_keys = {}
    for k, _ in pairs(val1) do
        check_keys[k] = true
    end

    for k, v in pairs(val2) do
        if not check_keys[k] then
            return false
        end

        if not compare_values(val1[k], val2[k]) then
            return false
        end

        check_keys[k] = nil
    end
    for k, _ in pairs(check_keys) do
        -- Not the same if any keys from val1 were not found in val2
        return false
    end
    return true
end

local test_count_pass = 0
local test_count_total = 0

local function run_test_summary()
    return test_count_pass, test_count_total
end

local function run_test(testname, func, input, should_work, output)
    local function status_line(name, status, value)
        local statusmap = { [true] = ":success", [false] = ":error" }
        if status ~= nil then
            name = name .. statusmap[status]
        end
        print(("[%s] %s"):format(name, serialise_value(value, false)))
    end

    local result = { pcall(func, unpack(input)) }
    local success = table.remove(result, 1)

    local correct = false
    if success == should_work and compare_values(result, output) then
        correct = true
        test_count_pass = test_count_pass + 1
    end
    test_count_total = test_count_total + 1

    local teststatus = { [true] = "PASS", [false] = "FAIL" }
    print(("==> Test [%d] %s: %s"):format(test_count_total, testname,
                                          teststatus[correct]))

    status_line("Input", nil, input)
    if not correct then
        status_line("Expected", should_work, output)
    end
    status_line("Received", success, result)
    print()

    return correct, result
end

local function run_test_group(tests)
    local function run_helper(name, func, input)
        if type(name) == "string" and #name > 0 then
            print("==> " .. name)
        end
        -- Not a protected call, these functions should never generate errors.
        func(unpack(input or {}))
        print()
    end

    for _, v in ipairs(tests) do
        -- Run the helper if "should_work" is missing
        if v[4] == nil then
            run_helper(unpack(v))
        else
            run_test(unpack(v))
        end
    end
end

-- Run a Lua script in a separate environment
local function run_script(script, env)
    local env = env or {}
    local func

    -- Use setfenv() if it exists, otherwise assume Lua 5.2 load() exists
    if _G.setfenv then
        func = loadstring(script)
        if func then
            setfenv(func, env)
        end
    else
        func = load(script, nil, nil, env)
    end

    if func == nil then
            error("Invalid syntax.")
    end
    func()

    return env
end

-- Export functions
return {
    serialise_value = serialise_value,
    file_load = file_load,
    file_save = file_save,
    compare_values = compare_values,
    run_test_summary = run_test_summary,
    run_test = run_test,
    run_test_group = run_test_group,
    run_script = run_script
}

-- vi:ai et sw=4 ts=4:
