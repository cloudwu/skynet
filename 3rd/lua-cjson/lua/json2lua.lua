#!/usr/bin/env lua

-- usage: json2lua.lua [json_file]
--
-- Eg:
-- echo '[ "testing" ]' | ./json2lua.lua
-- ./json2lua.lua test.json

local json = require "cjson"
local util = require "cjson.util"

local json_text = util.file_load(arg[1])
local t = json.decode(json_text)
print(util.serialise_value(t))
