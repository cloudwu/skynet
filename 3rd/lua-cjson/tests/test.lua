#!/usr/bin/env lua

-- Lua CJSON tests
--
-- Mark Pulford <mark@kyne.com.au>
--
-- Note: The output of this script is easier to read with "less -S"

local json = require "cjson"
local json_safe = require "cjson.safe"
local util = require "cjson.util"

local function gen_raw_octets()
    local chars = {}
    for i = 0, 255 do chars[i + 1] = string.char(i) end
    return table.concat(chars)
end

-- Generate every UTF-16 codepoint, including supplementary codes
local function gen_utf16_escaped()
    -- Create raw table escapes
    local utf16_escaped = {}
    local count = 0

    local function append_escape(code)
        local esc = ('\\u%04X'):format(code)
        table.insert(utf16_escaped, esc)
    end

    table.insert(utf16_escaped, '"')
    for i = 0, 0xD7FF do
        append_escape(i)
    end
    -- Skip 0xD800 - 0xDFFF since they are used to encode supplementary
    -- codepoints
    for i = 0xE000, 0xFFFF do
        append_escape(i)
    end
    -- Append surrogate pair for each supplementary codepoint
    for high = 0xD800, 0xDBFF do
        for low = 0xDC00, 0xDFFF do
            append_escape(high)
            append_escape(low)
        end
    end
    table.insert(utf16_escaped, '"')

    return table.concat(utf16_escaped)
end

function load_testdata()
    local data = {}

    -- Data for 8bit raw <-> escaped octets tests
    data.octets_raw = gen_raw_octets()
    data.octets_escaped = util.file_load("octets-escaped.dat")

    -- Data for \uXXXX -> UTF-8 test
    data.utf16_escaped = gen_utf16_escaped()

    -- Load matching data for utf16_escaped
    local utf8_loaded
    utf8_loaded, data.utf8_raw = pcall(util.file_load, "utf8.dat")
    if not utf8_loaded then
        data.utf8_raw = "Failed to load utf8.dat - please run genutf8.pl"
    end

    data.table_cycle = {}
    data.table_cycle[1] = data.table_cycle

    local big = {}
    for i = 1, 1100 do
        big = { { 10, false, true, json.null }, "string", a = big }
    end
    data.deeply_nested_data = big

    return data
end

function test_decode_cycle(filename)
    local obj1 = json.decode(util.file_load(filename))
    local obj2 = json.decode(json.encode(obj1))
    return util.compare_values(obj1, obj2)
end

-- Set up data used in tests
local Inf = math.huge;
local NaN = math.huge * 0;

local testdata = load_testdata()

local cjson_tests = {
    -- Test API variables
    { "Check module name, version",
      function () return json._NAME, json._VERSION end, { },
      true, { "cjson", "2.1.0" } },

    -- Test decoding simple types
    { "Decode string",
      json.decode, { '"test string"' }, true, { "test string" } },
    { "Decode numbers",
      json.decode, { '[ 0.0, -5e3, -1, 0.3e-3, 1023.2, 0e10 ]' },
      true, { { 0.0, -5000, -1, 0.0003, 1023.2, 0 } } },
    { "Decode null",
      json.decode, { 'null' }, true, { json.null } },
    { "Decode true",
      json.decode, { 'true' }, true, { true } },
    { "Decode false",
      json.decode, { 'false' }, true, { false } },
    { "Decode object with numeric keys",
      json.decode, { '{ "1": "one", "3": "three" }' },
      true, { { ["1"] = "one", ["3"] = "three" } } },
    { "Decode object with string keys",
      json.decode, { '{ "a": "a", "b": "b" }' },
      true, { { a = "a", b = "b" } } },
    { "Decode array",
      json.decode, { '[ "one", null, "three" ]' },
      true, { { "one", json.null, "three" } } },

    -- Test decoding errors
    { "Decode UTF-16BE [throw error]",
      json.decode, { '\0"\0"' },
      false, { "JSON parser does not support UTF-16 or UTF-32" } },
    { "Decode UTF-16LE [throw error]",
      json.decode, { '"\0"\0' },
      false, { "JSON parser does not support UTF-16 or UTF-32" } },
    { "Decode UTF-32BE [throw error]",
      json.decode, { '\0\0\0"' },
      false, { "JSON parser does not support UTF-16 or UTF-32" } },
    { "Decode UTF-32LE [throw error]",
      json.decode, { '"\0\0\0' },
      false, { "JSON parser does not support UTF-16 or UTF-32" } },
    { "Decode partial JSON [throw error]",
      json.decode, { '{ "unexpected eof": ' },
      false, { "Expected value but found T_END at character 21" } },
    { "Decode with extra comma [throw error]",
      json.decode, { '{ "extra data": true }, false' },
      false, { "Expected the end but found T_COMMA at character 23" } },
    { "Decode invalid escape code [throw error]",
      json.decode, { [[ { "bad escape \q code" } ]] },
      false, { "Expected object key string but found invalid escape code at character 16" } },
    { "Decode invalid unicode escape [throw error]",
      json.decode, { [[ { "bad unicode \u0f6 escape" } ]] },
      false, { "Expected object key string but found invalid unicode escape code at character 17" } },
    { "Decode invalid keyword [throw error]",
      json.decode, { ' [ "bad barewood", test ] ' },
      false, { "Expected value but found invalid token at character 20" } },
    { "Decode invalid number #1 [throw error]",
      json.decode, { '[ -+12 ]' },
      false, { "Expected value but found invalid number at character 3" } },
    { "Decode invalid number #2 [throw error]",
      json.decode, { '-v' },
      false, { "Expected value but found invalid number at character 1" } },
    { "Decode invalid number exponent [throw error]",
      json.decode, { '[ 0.4eg10 ]' },
      false, { "Expected comma or array end but found invalid token at character 6" } },

    -- Test decoding nested arrays / objects
    { "Set decode_max_depth(5)",
      json.decode_max_depth, { 5 }, true, { 5 } },
    { "Decode array at nested limit",
      json.decode, { '[[[[[ "nested" ]]]]]' },
      true, { {{{{{ "nested" }}}}} } },
    { "Decode array over nested limit [throw error]",
      json.decode, { '[[[[[[ "nested" ]]]]]]' },
      false, { "Found too many nested data structures (6) at character 6" } },
    { "Decode object at nested limit",
      json.decode, { '{"a":{"b":{"c":{"d":{"e":"nested"}}}}}' },
      true, { {a={b={c={d={e="nested"}}}}} } },
    { "Decode object over nested limit [throw error]",
      json.decode, { '{"a":{"b":{"c":{"d":{"e":{"f":"nested"}}}}}}' },
      false, { "Found too many nested data structures (6) at character 26" } },
    { "Set decode_max_depth(1000)",
      json.decode_max_depth, { 1000 }, true, { 1000 } },
    { "Decode deeply nested array [throw error]",
      json.decode, { string.rep("[", 1100) .. '1100' .. string.rep("]", 1100)},
      false, { "Found too many nested data structures (1001) at character 1001" } },

    -- Test encoding nested tables
    { "Set encode_max_depth(5)",
      json.encode_max_depth, { 5 }, true, { 5 } },
    { "Encode nested table as array at nested limit",
      json.encode, { {{{{{"nested"}}}}} }, true, { '[[[[["nested"]]]]]' } },
    { "Encode nested table as array after nested limit [throw error]",
      json.encode, { { {{{{{"nested"}}}}} } },
      false, { "Cannot serialise, excessive nesting (6)" } },
    { "Encode nested table as object at nested limit",
      json.encode, { {a={b={c={d={e="nested"}}}}} },
      true, { '{"a":{"b":{"c":{"d":{"e":"nested"}}}}}' } },
    { "Encode nested table as object over nested limit [throw error]",
      json.encode, { {a={b={c={d={e={f="nested"}}}}}} },
      false, { "Cannot serialise, excessive nesting (6)" } },
    { "Encode table with cycle [throw error]",
      json.encode, { testdata.table_cycle },
      false, { "Cannot serialise, excessive nesting (6)" } },
    { "Set encode_max_depth(1000)",
      json.encode_max_depth, { 1000 }, true, { 1000 } },
    { "Encode deeply nested data [throw error]",
      json.encode, { testdata.deeply_nested_data },
      false, { "Cannot serialise, excessive nesting (1001)" } },

    -- Test encoding simple types
    { "Encode null",
      json.encode, { json.null }, true, { 'null' } },
    { "Encode true",
      json.encode, { true }, true, { 'true' } },
    { "Encode false",
      json.encode, { false }, true, { 'false' } },
    { "Encode empty object",
      json.encode, { { } }, true, { '{}' } },
    { "Encode integer",
      json.encode, { 10 }, true, { '10' } },
    { "Encode string",
      json.encode, { "hello" }, true, { '"hello"' } },
    { "Encode Lua function [throw error]",
      json.encode, { function () end },
      false, { "Cannot serialise function: type not supported" } },

    -- Test decoding invalid numbers
    { "Set decode_invalid_numbers(true)",
      json.decode_invalid_numbers, { true }, true, { true } },
    { "Decode hexadecimal",
      json.decode, { '0x6.ffp1' }, true, { 13.9921875 } },
    { "Decode numbers with leading zero",
      json.decode, { '[ 0123, 00.33 ]' }, true, { { 123, 0.33 } } },
    { "Decode +-Inf",
      json.decode, { '[ +Inf, Inf, -Inf ]' }, true, { { Inf, Inf, -Inf } } },
    { "Decode +-Infinity",
      json.decode, { '[ +Infinity, Infinity, -Infinity ]' },
      true, { { Inf, Inf, -Inf } } },
    { "Decode +-NaN",
      json.decode, { '[ +NaN, NaN, -NaN ]' }, true, { { NaN, NaN, NaN } } },
    { "Decode Infrared (not infinity) [throw error]",
      json.decode, { 'Infrared' },
      false, { "Expected the end but found invalid token at character 4" } },
    { "Decode Noodle (not NaN) [throw error]",
      json.decode, { 'Noodle' },
      false, { "Expected value but found invalid token at character 1" } },
    { "Set decode_invalid_numbers(false)",
      json.decode_invalid_numbers, { false }, true, { false } },
    { "Decode hexadecimal [throw error]",
      json.decode, { '0x6' },
      false, { "Expected value but found invalid number at character 1" } },
    { "Decode numbers with leading zero [throw error]",
      json.decode, { '[ 0123, 00.33 ]' },
      false, { "Expected value but found invalid number at character 3" } },
    { "Decode +-Inf [throw error]",
      json.decode, { '[ +Inf, Inf, -Inf ]' },
      false, { "Expected value but found invalid token at character 3" } },
    { "Decode +-Infinity [throw error]",
      json.decode, { '[ +Infinity, Infinity, -Infinity ]' },
      false, { "Expected value but found invalid token at character 3" } },
    { "Decode +-NaN [throw error]",
      json.decode, { '[ +NaN, NaN, -NaN ]' },
      false, { "Expected value but found invalid token at character 3" } },
    { 'Set decode_invalid_numbers("on")',
      json.decode_invalid_numbers, { "on" }, true, { true } },

    -- Test encoding invalid numbers
    { "Set encode_invalid_numbers(false)",
      json.encode_invalid_numbers, { false }, true, { false } },
    { "Encode NaN [throw error]",
      json.encode, { NaN },
      false, { "Cannot serialise number: must not be NaN or Inf" } },
    { "Encode Infinity [throw error]",
      json.encode, { Inf },
      false, { "Cannot serialise number: must not be NaN or Inf" } },
    { "Set encode_invalid_numbers(\"null\")",
      json.encode_invalid_numbers, { "null" }, true, { "null" } },
    { "Encode NaN as null",
      json.encode, { NaN }, true, { "null" } },
    { "Encode Infinity as null",
      json.encode, { Inf }, true, { "null" } },
    { "Set encode_invalid_numbers(true)",
      json.encode_invalid_numbers, { true }, true, { true } },
    { "Encode NaN",
      json.encode, { NaN }, true, { "nan" } },
    { "Encode Infinity",
      json.encode, { Inf }, true, { "inf" } },
    { 'Set encode_invalid_numbers("off")',
      json.encode_invalid_numbers, { "off" }, true, { false } },

    -- Test encoding tables
    { "Set encode_sparse_array(true, 2, 3)",
      json.encode_sparse_array, { true, 2, 3 }, true, { true, 2, 3 } },
    { "Encode sparse table as array #1",
      json.encode, { { [3] = "sparse test" } },
      true, { '[null,null,"sparse test"]' } },
    { "Encode sparse table as array #2",
      json.encode, { { [1] = "one", [4] = "sparse test" } },
      true, { '["one",null,null,"sparse test"]' } },
    { "Encode sparse array as object",
      json.encode, { { [1] = "one", [5] = "sparse test" } },
      true, { '{"1":"one","5":"sparse test"}' } },
    { "Encode table with numeric string key as object",
      json.encode, { { ["2"] = "numeric string key test" } },
      true, { '{"2":"numeric string key test"}' } },
    { "Set encode_sparse_array(false)",
      json.encode_sparse_array, { false }, true, { false, 2, 3 } },
    { "Encode table with incompatible key [throw error]",
      json.encode, { { [false] = "wrong" } },
      false, { "Cannot serialise boolean: table key must be a number or string" } },

    -- Test escaping
    { "Encode all octets (8-bit clean)",
      json.encode, { testdata.octets_raw }, true, { testdata.octets_escaped } },
    { "Decode all escaped octets",
      json.decode, { testdata.octets_escaped }, true, { testdata.octets_raw } },
    { "Decode single UTF-16 escape",
      json.decode, { [["\uF800"]] }, true, { "\239\160\128" } },
    { "Decode all UTF-16 escapes (including surrogate combinations)",
      json.decode, { testdata.utf16_escaped }, true, { testdata.utf8_raw } },
    { "Decode swapped surrogate pair [throw error]",
      json.decode, { [["\uDC00\uD800"]] },
      false, { "Expected value but found invalid unicode escape code at character 2" } },
    { "Decode duplicate high surrogate [throw error]",
      json.decode, { [["\uDB00\uDB00"]] },
      false, { "Expected value but found invalid unicode escape code at character 2" } },
    { "Decode duplicate low surrogate [throw error]",
      json.decode, { [["\uDB00\uDB00"]] },
      false, { "Expected value but found invalid unicode escape code at character 2" } },
    { "Decode missing low surrogate [throw error]",
      json.decode, { [["\uDB00"]] },
      false, { "Expected value but found invalid unicode escape code at character 2" } },
    { "Decode invalid low surrogate [throw error]",
      json.decode, { [["\uDB00\uD"]] },
      false, { "Expected value but found invalid unicode escape code at character 2" } },

    -- Test locale support
    --
    -- The standard Lua interpreter is ANSI C online doesn't support locales
    -- by default. Force a known problematic locale to test strtod()/sprintf().
    { "Set locale to cs_CZ (comma separator)", function ()
        os.setlocale("cs_CZ")
        json.new()
    end },
    { "Encode number under comma locale",
      json.encode, { 1.5 }, true, { '1.5' } },
    { "Decode number in array under comma locale",
      json.decode, { '[ 10, "test" ]' }, true, { { 10, "test" } } },
    { "Revert locale to POSIX", function ()
        os.setlocale("C")
        json.new()
    end },

    -- Test encode_keep_buffer() and enable_number_precision()
    { "Set encode_keep_buffer(false)",
      json.encode_keep_buffer, { false }, true, { false } },
    { "Set encode_number_precision(3)",
      json.encode_number_precision, { 3 }, true, { 3 } },
    { "Encode number with precision 3",
      json.encode, { 1/3 }, true, { "0.333" } },
    { "Set encode_number_precision(14)",
      json.encode_number_precision, { 14 }, true, { 14 } },
    { "Set encode_keep_buffer(true)",
      json.encode_keep_buffer, { true }, true, { true } },

    -- Test config API errors
    -- Function is listed as '?' due to pcall
    { "Set encode_number_precision(0) [throw error]",
      json.encode_number_precision, { 0 },
      false, { "bad argument #1 to '?' (expected integer between 1 and 14)" } },
    { "Set encode_number_precision(\"five\") [throw error]",
      json.encode_number_precision, { "five" },
      false, { "bad argument #1 to '?' (number expected, got string)" } },
    { "Set encode_keep_buffer(nil, true) [throw error]",
      json.encode_keep_buffer, { nil, true },
      false, { "bad argument #2 to '?' (found too many arguments)" } },
    { "Set encode_max_depth(\"wrong\") [throw error]",
      json.encode_max_depth, { "wrong" },
      false, { "bad argument #1 to '?' (number expected, got string)" } },
    { "Set decode_max_depth(0) [throw error]",
      json.decode_max_depth, { "0" },
      false, { "bad argument #1 to '?' (expected integer between 1 and 2147483647)" } },
    { "Set encode_invalid_numbers(-2) [throw error]",
      json.encode_invalid_numbers, { -2 },
      false, { "bad argument #1 to '?' (invalid option '-2')" } },
    { "Set decode_invalid_numbers(true, false) [throw error]",
      json.decode_invalid_numbers, { true, false },
      false, { "bad argument #2 to '?' (found too many arguments)" } },
    { "Set encode_sparse_array(\"not quite on\") [throw error]",
      json.encode_sparse_array, { "not quite on" },
      false, { "bad argument #1 to '?' (invalid option 'not quite on')" } },

    { "Reset Lua CJSON configuration", function () json = json.new() end },
    -- Wrap in a function to ensure the table returned by json.new() is used
    { "Check encode_sparse_array()",
      function (...) return json.encode_sparse_array(...) end, { },
      true, { false, 2, 10 } },

    { "Encode (safe) simple value",
      json_safe.encode, { true },
      true, { "true" } },
    { "Encode (safe) argument validation [throw error]",
      json_safe.encode, { "arg1", "arg2" },
      false, { "bad argument #1 to '?' (expected 1 argument)" } },
    { "Decode (safe) error generation",
      json_safe.decode, { "Oops" },
      true, { nil, "Expected value but found invalid token at character 1" } },
    { "Decode (safe) error generation after new()",
      function(...) return json_safe.new().decode(...) end, { "Oops" },
      true, { nil, "Expected value but found invalid token at character 1" } },
}

print(("==> Testing Lua CJSON version %s\n"):format(json._VERSION))

util.run_test_group(cjson_tests)

for _, filename in ipairs(arg) do
    util.run_test("Decode cycle " .. filename, test_decode_cycle, { filename },
                  true, { true })
end

local pass, total = util.run_test_summary()

if pass == total then
    print("==> Summary: all tests succeeded")
else
    print(("==> Summary: %d/%d tests failed"):format(total - pass, total))
    os.exit(1)
end

-- vi:ai et sw=4 ts=4:
