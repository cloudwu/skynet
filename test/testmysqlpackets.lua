-- Use a test database and set mysql_user and mysql_database in the Skynet
-- config. Set the server's max_allowed_packet to at least 64 MiB.
local skynet = require "skynet"
local mysql = require "skynet.db.mysql"

local MAX_PACKET_PAYLOAD = 0xffffff

local function checked(result)
	assert(not result.badresult, result.err)
	return result
end

local function check_large_packets(db)
	local prefix, suffix = "SELECT 42 AS answer /*", "*/"
	for _, size in ipairs {MAX_PACKET_PAYLOAD + 1, MAX_PACKET_PAYLOAD - 1,
		MAX_PACKET_PAYLOAD, 2 * MAX_PACKET_PAYLOAD} do
		local query = prefix .. ("x"):rep(size - 1 - #prefix - #suffix) .. suffix
		assert(checked(db:query(query))[1].answer == 42, size)
	end

	-- The first value and its four-byte length prefix fill a packet exactly.
	-- The second value uses an eight-byte length prefix with a 0xfe marker.
	for _, len in ipairs {MAX_PACKET_PAYLOAD - 4, MAX_PACKET_PAYLOAD + 1} do
		local row = checked(db:query(("SELECT REPEAT('x', %d) AS value"):format(len)))[1]
		assert(row.value == ("x"):rep(len), len)
	end

	local stmt = checked(db:prepare("SELECT LENGTH(?) AS size"))
	local value = ("x"):rep(MAX_PACKET_PAYLOAD + 1)
	assert(checked(db:execute(stmt, value))[1].size == #value)
	db:stmt_close(stmt)
end

local function check_concurrent_commands(db)
	local caller = coroutine.running()
	local ok, small
	skynet.fork(function()
		ok, small = pcall(db.query, db, "SELECT 43 AS answer")
		skynet.wakeup(caller)
	end)
	local query = "SELECT 42 AS answer /*" .. ("x"):rep(MAX_PACKET_PAYLOAD) .. "*/"
	assert(checked(db:query(query))[1].answer == 42)
	if ok == nil then
		skynet.wait(caller)
	end
	assert(ok, small)
	assert(checked(small)[1].answer == 43)
end

skynet.start(function()
	local db = mysql.connect {
		host = skynet.getenv("mysql_host") or "127.0.0.1",
		port = tonumber(skynet.getenv("mysql_port")) or 3306,
		user = assert(skynet.getenv("mysql_user")),
		password = skynet.getenv("mysql_password") or "",
		database = assert(skynet.getenv("mysql_database")),
		plugin_auth = true,
		max_packet_size = 64 * 1024 * 1024,
	}
	check_large_packets(db)
	check_concurrent_commands(db)
	db:disconnect()
	skynet.error("MySQL packet tests passed")
	skynet.fork(skynet.exit)
end)
