local skynet = require "skynet"
local rediscluster = require "skynet.db.redis.cluster"

local test_more = ...

skynet.start(function ()
	local db = rediscluster.new({
		{host="127.0.0.1",port=7000},
		{host="127.0.0.1",port=7001},},
		{read_slave=true,auth=nil,db=0,}
	)
	db:del("list")
	db:del("map")
	db:rpush("list",1,2,3)
	local list = db:lrange("list",0,-1)
	for i,v in ipairs(list) do
		print(v)
	end
	db:hmset("map","key1",1,"key2",2)
	local map = db:hgetall("map")
	for i=1,#map,2 do
		local key = map[i]
		local val = map[i+1]
		print(key,val)
	end
	-- test MOVED
	db:flush_slots_cache()
	print(db:set("A",1))
	print(db:get("A"))
	-- reconnect
	local cnt = 0
	for name,conn in pairs(db.connections) do
		print(name,conn)
		cnt = cnt + 1
	end
	print("cnt:",cnt)
	db:close_all_connection()
	print(db:set("A",1))
	print(db:del("A"))

	local slot = db:keyslot("{foo}")
	local conn = db:get_connection_by_slot(slot)
	-- You must ensure keys at one slot: use same key or hash tags
	local ret = conn:pipeline({
		{"hincrby", "{foo}hello", 1, 1},
		{"del", "{foo}hello"},
		{"hmset", "{foo}hello", 1, 1, 2, 2, 3, 3},
		{"hgetall", "{foo}hello"},
	},{})
	print(ret[1].ok,ret[1].out)
	print(ret[2].ok,ret[2].out)
	print(ret[3].ok,ret[3].out)
	print(ret[4].ok)
	if ret[4].ok then
		for i,v in ipairs(ret[4].out) do
			print(v)
		end
	else
		print(ret[4].out)
	end
	-- dbsize/info/keys
	local conn = db:get_random_connection()
	print("dbsize:",conn:dbsize())
	print("info:",conn:info())
	local keys = conn:keys("list*")
	for i,key in ipairs(keys) do
		print(key)
	end
	print("cluster nodes")
	local nodes = db:cluster("nodes")
	print(nodes)
	print("cluster slots")
	local slots = db:cluster("slots")
	for i,slot_map in ipairs(slots) do
		local start_slot = slot_map[1]
		local end_slot = slot_map[2]
		local master_node = slot_map[3]
		print(start_slot,end_slot)
		for i,v in ipairs(master_node) do
			print(v)
		end
		for i=4,#slot_map do
			local slave_node = slot_map[i]
			for i,v in ipairs(slave_node) do
				print(v)
			end
		end
	end

	if not test_more then
		skynet.exit()
		return
	end
	local last = false
	while not last do
		last = db:get("__last__")
		if last == nil then
			last = 0
		end
		last = tonumber(last)
	end
	for val=last,1000000000 do
		local ok,errmsg = pcall(function ()
			local key = string.format("foo%s",val)
			db:set(key,val)
			print(key,db:get(key))
			db:set("__last__",val)
		end)
		if not ok then
			print("error:",errmsg)
		end
	end
	skynet.exit()
end)
