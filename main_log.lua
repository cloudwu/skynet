local skynet = require "skynet"

print("Log server start")

local log = skynet.launch("snlua","globallog.lua")
print("log",log)

skynet.exit()
