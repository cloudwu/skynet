local skynet = require "skynet"

print("Log server start")

local log = skynet.launch("snlua","globallog.lua")
print("log",log)

local db = skynet.launch("snlua","simpledb.lua")
print("simpledb",db)

skynet.exit()
