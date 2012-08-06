local skynet = require "skynet"

print("Server start")

local console = skynet.launch("snlua","console.lua")
print("console",console)
local watchdog = skynet.launch("snlua","watchdog.lua")
print("watchdog",watchdog)
local gate = skynet.launch("gate","8888 4 0")
print("gate",gate)
local db = skynet.launch("snlua","simpledb.lua")
print("simpledb",db)

skynet.exit()
