local skynet = require "skynet"
local kvdb = require "kvdb"

local function dbname(i)
	return "db"..i
end
skynet.start(function()
	for i=1,10 do
		kvdb.new(dbname(i))
	end
	local idx=1
	for i=1,10 do
		local db=dbname(i)
		kvdb.set(db,"A",idx)
		idx=idx+1
		kvdb.set(db,"B",idx)
		idx=idx+1
	end
	for i=1,10 do
		local db=dbname(i)
		print(db,kvdb.get(db,"A"),kvdb.get(db,"B"))
	end
end)
