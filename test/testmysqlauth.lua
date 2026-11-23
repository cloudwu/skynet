local skynet = require "skynet"
local mysql = require "skynet.db.mysql"

local opts = ... or {
	host = "127.0.0.1",
	port = 3306,
	user = "root",
	password = "123456",
	plugin_auth = true,
	allow_public_key_retrieval = true
}

local function session(opts)
	local db = mysql.connect(opts)
	db:set_compact_arrays(true)
	local rows = db:query("SELECT CONNECTION_ID(), CURRENT_USER(), VERSION()")
	if opts.plugin_auth then
		local account = db:query("SHOW CREATE USER CURRENT_USER()")
		assert(account[1], account.err)
		local plugin = account[1][1]:match("IDENTIFIED WITH '([^']+)'")
		assert(plugin == "caching_sha2_password", "expected a caching_sha2_password test account")
	end
	db:disconnect()

	assert(rows[1], rows.err)
	return table.unpack(rows[1])
end

skynet.start(function()
	local plugin = opts.plugin_auth and "caching_sha2_password" or "mysql_native_password"
	assert(opts.password ~= "", "use a nonempty password to exercise authentication")
	local first, user, version = session(opts)
	opts.server_public_key_path = nil
	opts.allow_public_key_retrieval = false
	local second, second_user = session(opts)
	assert(first ~= second and user == second_user, "expected a new session for the same account")
	print(("PASS MySQL %s: %s authenticated with %s (connections %s, %s)")
		:format(version, user, plugin, first, second))
end)
