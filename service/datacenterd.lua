local skynet = require "skynet"

local command = {}
local database = {}
local wait_queue = {}
local mode = {}

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
		return ret, value
	else
		if db[key] == nil then
			db[key] = {}
		end
		return update(db[key], value, ...)
	end
end

local function wakeup(db, key1, key2, value, ...)
	if key1 == nil then
		return
	end
	local q = db[key1]
	if q == nil then
		return
	end
	if q[mode] == "queue" then
		db[key1] = nil
		if value then
			-- throw error because can't wake up a branch
			for _,v in ipairs(q) do
				local session = v[1]
				local source = v[2]
				skynet.redirect(source, 0, "error", session, "")
			end
		else
			return q
		end
	else
		-- it's branch
		return wakeup(q , key2, value, ...)
	end
end

function command.UPDATE(...)
	local ret, value = update(database, ...)
	if ret or value == nil then
		return ret
	end
	local q = wakeup(wait_queue, ...)
	if q then
		for _, v in ipairs(q) do
			local session = v[1]
			local source = v[2]
			skynet.redirect(source, 0, "response", session, skynet.pack(value))
		end
	end
end

local function waitfor(session, source, db, key1, key2, ...)
	if key2 == nil then
		-- push queue
		local q = db[key1]
		if q == nil then
			q = { [mode] = "queue" }
			db[key1] = q
		else
			assert(q[mode] == "queue")
		end
		table.insert(q, { session, source })
	else
		local q = db[key1]
		if q == nil then
			q = { [mode] = "branch" }
			db[key1] = q
		else
			assert(q[mode] == "branch")
		end
		return waitfor(session, source, q, key2, ...)
	end
end

skynet.start(function()
	skynet.dispatch("lua", function (session, source, cmd, ...)
		if cmd == "WAIT" then
			local ret = command.QUERY(...)
			if ret then
				skynet.ret(skynet.pack(ret))
			else
				waitfor(session, source, wait_queue, ...)
			end
		else
			local f = assert(command[cmd])
			skynet.ret(skynet.pack(f(...)))
		end
	end)
end)
