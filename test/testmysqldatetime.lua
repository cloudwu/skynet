-- Use a test database and set mysql_user and mysql_database in the Skynet
-- config.
local skynet = require "skynet"
local mysql = require "skynet.db.mysql"

local function checked(result)
	assert(not result.badresult, result.err)
	return result
end

local function check_datetime(db)
	checked(db:query("SET time_zone = '+00:00', sql_mode = ''"))
	checked(db:query [[
		CREATE TEMPORARY TABLE skynet_protocol_dates (
			dt DATETIME(6), ts TIMESTAMP(6), plain DATETIME, midnight DATETIME,
			zero_date DATETIME, missing DATETIME, marker INT
		)
	]])
	checked(db:query [[
		INSERT INTO skynet_protocol_dates VALUES (
			'2026-01-02 03:04:05.123456', '2026-01-02 03:04:05.000042',
			'2026-01-02 03:04:05', '2026-01-02 00:00:00',
			'0000-00-00 00:00:00', NULL, 42
		)
	]])
	local stmt = checked(db:prepare("SELECT * FROM skynet_protocol_dates"))
	local row = checked(db:execute(stmt))[1]
	db:stmt_close(stmt)
	assert(row.dt == "2026-01-02 03:04:05.123456", row.dt)
	assert(row.ts == "2026-01-02 03:04:05.000042", row.ts)
	assert(row.plain == "2026-01-02 03:04:05", row.plain)
	assert(row.midnight == "2026-01-02 00:00:00", row.midnight)
	assert(row.zero_date == "0000-00-00 00:00:00", row.zero_date)
	assert(row.missing == nil and row.marker == 42)
end

skynet.start(function()
	local db = mysql.connect {
		host = skynet.getenv("mysql_host") or "127.0.0.1",
		port = tonumber(skynet.getenv("mysql_port")) or 3306,
		user = assert(skynet.getenv("mysql_user")),
		password = skynet.getenv("mysql_password") or "",
		database = assert(skynet.getenv("mysql_database")),
		plugin_auth = true,
	}
	check_datetime(db)
	db:disconnect()
	skynet.error("MySQL datetime tests passed")
	skynet.fork(skynet.exit)
end)
