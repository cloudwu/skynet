local skynet = require "skynet"
local redis = require "redis"

skynet.start(function()
	local db = redis.connect "main"
	db:batch "write"	-- ignore results
		db:set("A", "hello")
		db:set("B", "world")
	db:batch "end"

	db:batch "read"
		db:get("A")
		db:get("B")
	local r = db:batch "end" -- return all results in a table
	for k,v in pairs(r) do
		print(k,v)
	end
	print(db:exists "A")
	print(db:get "A")
	print(db:set("A","hello world"))
	print(db:get("A"))
	skynet.exit()
end)

