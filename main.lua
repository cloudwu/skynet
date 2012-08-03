local skynet = require "skynet"

print("Server start")

local console = skynet.command("LAUNCH","snlua console.lua")
print("console",console)
local watchdog = skynet.command("LAUNCH","snlua watchdog.lua")
print("watchdog",watchdog)
local gate = skynet.command("LAUNCH","gate 8888 4 0")
print("gate",gate)

skynet.command("EXIT")
