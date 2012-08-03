local skynet = require "skynet"

print("Log server start")

local log = skynet.command("LAUNCH","snlua globallog.lua")
print("log",log)

skynet.command("EXIT")
