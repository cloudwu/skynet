local skynet = require "skynet"
local mongo = require "skynet.db.mongo"
local bson = require "bson"

local host, db_name = ...

function test_insert_without_index()
	local db = mongo.client({host = host})

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	local ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ret and ret.n == 1)
end

function test_insert_with_index()
	local db = mongo.client({host = host})

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index"})

	local ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ret and ret.n == 0)
end

function test_find_and_remove()
	local db = mongo.client({host = host})

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {test_key2 = -1}, {unique = true, name = "test_index"})

	local ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 1})
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 2})
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:safe_insert({test_key = 2, test_key2 = 3})
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:findOne({test_key2 = 1})
	assert(ret and ret.test_key2 == 1)

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
	local db = mongo.client({host = host})

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index", expireAfterSeconds = 1, })
	db[db_name].testdb:ensureIndex({test_date = 1}, {expireAfterSeconds = 1, })

	local ret = db[db_name].testdb:safe_insert({test_key = 1, test_date = bson.date(os.time())})
	assert(ret and ret.n == 1)

	local ret = db[db_name].testdb:findOne({test_key = 1})
	assert(ret and ret.test_key == 1)

	for i = 1, 1000 do
		skynet.sleep(1);

		local ret = db[db_name].testdb:findOne({test_key = 1})
		if ret == nil then
			return
		end
	end

	assert(false, "test expire index failed");
end

skynet.start(function()
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
