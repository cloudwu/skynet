local skynet = require "skynet"
local memory = require "memory"

memory.dumpinfo()
memory.dump()

print("Total memory:", memory.total())
print("Total block:", memory.block())

skynet.exit()
