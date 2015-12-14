local skynet = require "skynet"
local memory = require "memory"

memory.dumpinfo()
--memory.dump()
local info = memory.info()
for k,v in pairs(info) do
	print(string.format(":%08x %gK",k,v/1024))
end

print("Total c memory:", memory.total())
print("Total c block:", memory.block())

skynet.start(function() skynet.exit() end)
