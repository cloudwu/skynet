local skynet = require "skynet"

local command = {}
local database = {}

local function query(db, key, ...)
	if key == nil then
		return db
	else
		return query(db[key], ...)
	end
end

function command.QUERY(key, ...)
	local d = database[key]
	if d then
		return query(d, ...)
	end
end

local function update(db, key, value, ...)
	if select("#",...) == 0 then
		local ret = db[key]
		db[key] = value
		return ret
	else
		if db[key] == nil then
			db[key] = {}
		end
		return update(db[key], value, ...)
	end
end

function command.UPDATE(...)
	return update(database, ...)
end

skynet.start(function()
	skynet.dispatch("lua", function (_, source, cmd, ...)
		local f = assert(command[cmd])
		skynet.ret(skynet.pack(f(...)))
	end)
end)
