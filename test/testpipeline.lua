local skynet = require "skynet"
local redis = require "skynet.db.redis"

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

	db.pipelining = function (self, block)
		local ops = {}

		block(setmetatable({}, {
			__index = function (_, name)
				return function (_, ...)
					table.insert(ops, {name, ...})
				end
			end
		}))

		return self:pipeline(ops)
	end

	do
		print("test function")
		local ret = db:pipelining(function (red)
			red:multi()
			red:hincrby("hello", 1, 1)
			red:del("hello")
			red:hmset("hello", 1, 1, 2, 2, 3, 3)
			red:hgetall("hello")
			red:exec()
		end)
		-- ret is the result of red:exec()
		for k, v in pairs(read_table(ret[4])) do
			print(k, v)
		end
	end

	do
		print("test table")
		local ret = db:pipeline({
			{"hincrby", "hello", 1, 1},
			{"del", "hello"},
			{"hmset", "hello", 1, 1, 2, 2, 3, 3},
			{"hgetall", "hello"},
		}, {})	-- offer a {} for result

		print(ret[1].out)
		print(ret[2].out)
		print(ret[3].out)

		for k, v in pairs(read_table(ret[4].out)) do
			print(k, v)
		end
	end

	db:disconnect()
	skynet.exit()
end)

