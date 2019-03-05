local skynet = require "skynet"
local mongo = require "skynet.db.mongo"
local bson = require "bson"

local host, port, db_name, username, password = ...
if port then
	port = math.tointeger(port)
end

-- print(host, port, db_name, username, password)

local function _create_client()
	return mongo.client(
		{
			host = host, port = port,
			username = username, password = password,
			authdb = db_name,
		}
	)
end

function test_auth()
	local c = mongo.client(
		{
			host = host, port = port,
		}
	)
	db = c[db_name]
	db:auth(username, password)

	db.testdb:dropIndex("*")
	db.testdb:drop()

	local ok, err, ret = db.testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db.testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

function test_insert_without_index()
	local db = _create_client()
	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

function test_insert_with_index()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index"})

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ok and ret and ret.n == 1)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ok == false and string.find(err, "duplicate key error"))  
end

function test_find_and_remove()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {test_key2 = -1}, {unique = true, name = "test_index"})

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 1})
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 2})
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 2, test_key2 = 3})
	assert(ok and ret and ret.n == 1, err)

	local ret = db[db_name].testdb:findOne({test_key2 = 1})
	assert(ret and ret.test_key2 == 1, err)

	local ret = db[db_name].testdb:find({test_key2 = {['$gt'] = 0}}):sort({test_key = 1}, {test_key2 = -1}):skip(1):limit(1)
 	assert(ret:count() == 3)
 	assert(ret:count(true) == 1)
	if ret:hasNext() then
		ret = ret:next()
	end
	assert(ret and ret.test_key2 == 1)

	db[db_name].testdb:delete({test_key = 1})
	db[db_name].testdb:delete({test_key = 2})

	local ret = db[db_name].testdb:findOne({test_key = 1})
	assert(ret == nil)
end


function test_expire_index()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index", expireAfterSeconds = 1, })
	db[db_name].testdb:ensureIndex({test_date = 1}, {expireAfterSeconds = 1, })

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_date = bson.date(os.time())})
	assert(ok and ret and ret.n == 1, err)

	local ret = db[db_name].testdb:findOne({test_key = 1})
	assert(ret and ret.test_key == 1)

	for i = 1, 60 do
		skynet.sleep(100);
		print("check expire", i)
		local ret = db[db_name].testdb:findOne({test_key = 1})
		if ret == nil then
			return
		end
	end
	print("test expire index failed")
	assert(false, "test expire index failed");
end

skynet.start(function()
	if username then
		print("Test auth")
		test_auth()
	end
	print("Test insert without index")
	test_insert_without_index()
	print("Test insert index")
	test_insert_with_index()
	print("Test find and remove")
	test_find_and_remove()
	print("Test expire index")
	test_expire_index()
	print("mongodb test finish.");
end)
