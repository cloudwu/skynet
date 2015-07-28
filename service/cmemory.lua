local skynet = require "skynet"
local memory = require "memory"

memory.dumpinfo()
--memory.dump()
local info = memory.info()
for k,v in pairs(info) do
	print(string.format(":%08x %gK",k,v/1024))
end

print("Total memory:", memory.total())
print("Total block:", memory.block())

skynet.start(function() skynet.exit() end)
