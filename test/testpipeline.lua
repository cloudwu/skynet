local skynet = require "skynet"
local redis = require "redis"

local conf = {
	host = "127.0.0.1",
	port = 6379,
	db = 0
}

local function read_table(t)
    local result = { }
    for i = 1, #t, 2 do result[t[i]] = t[i + 1] end
    return result
end

skynet.start(function()
	local db = redis.connect(conf)

	local ret = db:pipeline {
		{"hincrby", "hello", 1, 1},
		{"del", "hello"},
		{"hincrby", "hello", 3, 1},
		{"hgetall", "hello"},
	}

	print(ret[1].out)
	print(ret[2].out)
	print(ret[3].out)

	for k, v in pairs(read_table(ret[4].out)) do
		print(k, v)
	end

	db:disconnect()
	skynet.exit()
end)

