local bson = require "bson"

require "skynet.socket"

local socketchannel	= require "skynet.socketchannel"
local skynet = require "skynet"
local driver = require "skynet.mongo.driver"
local md5 =	require	"md5"
local crypt = require "skynet.crypt"
local rawget = rawget
local assert = assert
local table = table

local bson_encode =	bson.encode
local bson_encode_order	= bson.encode_order
local bson_decode =	bson.decode
local bson_int64 = bson.int64
local empty_bson = bson_encode {}

local mongo	= {}
mongo.null = assert(bson.null)
mongo.maxkey = assert(bson.maxkey)
mongo.minkey = assert(bson.minkey)
mongo.type = assert(bson.type)

local mongo_cursor = {}
local cursor_meta =	{
	__index	= mongo_cursor,
}

local mongo_client = {}

local client_meta =	{
	__index	= function(self, key)
		return rawget(mongo_client,	key) or	self:getDB(key)
	end,
	__tostring = function (self)
		local port_string
		if self.port then
			port_string	= ":" .. tostring(self.port)
		else
			port_string	= ""
		end

		return "[mongo client :	" .. self.host .. port_string .."]"
	end,
	-- DO NOT need disconnect, because channel will	shutdown during	gc
}

local mongo_db = {}

local db_meta =	{
	__index	= function (self, key)
		return rawget(mongo_db,	key) or	self:getCollection(key)
	end,
	__tostring = function (self)
		return "[mongo db :	" .. self.name .. "]"
	end
}

local mongo_collection = {}
local collection_meta =	{
	__index	= function(self, key)
		return rawget(mongo_collection,	key) or	self:getCollection(key)
	end	,
	__tostring = function (self)
		return "[mongo collection :	" .. self.full_name	.. "]"
	end
}

local function dispatch_reply(so)
	local len_reply	= so:read(4)
	local reply	= so:read(driver.length(len_reply))
	local result = {}
	local succ,	reply_id, document = driver.reply(reply)
	result.document	= document
	result.data	= reply
	return reply_id, succ, result
end

local function __parse_addr(addr)
	local host,	port = string.match(addr, "([^:]+):(.+)")
	return host, tonumber(port)
end

local auth_method = {}

local function mongo_auth(mongoc)
	local user = rawget(mongoc,	"username")
	local pass = rawget(mongoc,	"password")
	local authmod = rawget(mongoc, "authmod") or "scram_sha1"
	authmod = "auth_" ..  authmod
	local authdb = rawget(mongoc, "authdb")
	if authdb then
		authdb = mongo_client.getDB(mongoc, authdb)	-- mongoc has not set metatable yet
	end

	return function()
		if user	~= nil and pass	~= nil then
			-- autmod can be "mongodb_cr" or "scram_sha1"
			local auth_func = auth_method[authmod]
			assert(auth_func , "Invalid authmod")
			assert(auth_func(authdb or mongoc, user, pass))
		end
		local rs_data =	mongoc:runCommand("ismaster")
		if rs_data.ok == 1 then
			if rs_data.hosts then
				local backup = {}
				for	_, v in	ipairs(rs_data.hosts) do
					local host,	port = __parse_addr(v)
					table.insert(backup, {host = host, port	= port})
				end
				mongoc.__sock:changebackup(backup)
			end
			if rs_data.ismaster	then
				return
			elseif rs_data.primary then
				local host,	port = __parse_addr(rs_data.primary)
				mongoc.host	= host
				mongoc.port	= port
				mongoc.__sock:changehost(host, port)
			else
				-- socketchannel would try the next host in backup list
				error ("No primary return : " .. tostring(rs_data.me))
			end
		end
	end
end

function mongo.client( conf	)
	local first	= conf
	local backup = nil
	if conf.rs then
		first =	conf.rs[1]
		backup = conf.rs
	end
	local obj =	{
		host = first.host,
		port = first.port or 27017,
		username = first.username,
		password = first.password,
		authmod = first.authmod,
		authdb = first.authdb,
	}

	obj.__id = 0
	obj.__sock = socketchannel.channel {
		host = obj.host,
		port = obj.port,
		response = dispatch_reply,
		auth = mongo_auth(obj),
		backup = backup,
		nodelay = true,
		overload = conf.overload,
	}
	setmetatable(obj, client_meta)
	obj.__sock:connect(true)	-- try connect only	once
	return obj
end

function mongo_client:getDB(dbname)
	local db = {
		connection = self,
		name = dbname,
		full_name =	dbname,
		database = false,
		__cmd =	dbname .. "." .. "$cmd",
	}
	db.database	= db

	return setmetatable(db,	db_meta)
end

function mongo_client:disconnect()
	if self.__sock then
		local so = self.__sock
		self.__sock	= false
		so:close()
	end
end

function mongo_client:genId()
	local id = self.__id + 1
	self.__id =	id
	return id
end

function mongo_client:runCommand(...)
	if not self.admin then
		self.admin = self:getDB	"admin"
	end
	return self.admin:runCommand(...)
end

function auth_method:auth_mongodb_cr(user,password)
	local password = md5.sumhexa(string.format("%s:mongo:%s",user,password))
	local result= self:runCommand "getnonce"
	if result.ok ~=1 then
		return false
	end

	local key =	md5.sumhexa(string.format("%s%s%s",result.nonce,user,password))
	local result= self:runCommand ("authenticate",1,"user",user,"nonce",result.nonce,"key",key)
	return result.ok ==	1
end

local function salt_password(password, salt, iter)
	salt = salt .. "\0\0\0\1"
	local output = crypt.hmac_sha1(password, salt)
	local inter = output
	for i=2,iter do
		inter = crypt.hmac_sha1(password, inter)
		output = crypt.xor_str(output, inter)
	end
	return output
end

function auth_method:auth_scram_sha1(username,password)
	local user = string.gsub(string.gsub(username, '=', '=3D'), ',' , '=2C')
	local nonce = crypt.base64encode(crypt.randomkey())
	local first_bare = "n="  .. user .. ",r="  .. nonce
	local sasl_start_payload = crypt.base64encode("n,," .. first_bare)
	local r

	r = self:runCommand("saslStart",1,"autoAuthorize",1,"mechanism","SCRAM-SHA-1","payload",sasl_start_payload)
	if r.ok ~= 1 then
		return false
	end

	local conversationId = r['conversationId']
	local server_first = r['payload']
	local parsed_s = crypt.base64decode(server_first)
	local parsed_t = {}
	for k, v in string.gmatch(parsed_s, "(%w+)=([^,]*)") do
		parsed_t[k] = v
	end
	local iterations = tonumber(parsed_t['i'])
	local salt = parsed_t['s']
	local rnonce = parsed_t['r']

	if not string.sub(rnonce, 1, 12) == nonce then
		skynet.error("Server returned an invalid nonce.")
		return false
	end
	local without_proof = "c=biws,r=" .. rnonce
	local pbkdf2_key = md5.sumhexa(string.format("%s:mongo:%s",username,password))
	local salted_pass = salt_password(pbkdf2_key, crypt.base64decode(salt), iterations)
	local client_key = crypt.hmac_sha1(salted_pass, "Client Key")
	local stored_key = crypt.sha1(client_key)
	local auth_msg = first_bare .. ',' .. parsed_s .. ',' .. without_proof
	local client_sig = crypt.hmac_sha1(stored_key, auth_msg)
	local client_key_xor_sig = crypt.xor_str(client_key, client_sig)
	local client_proof = "p=" .. crypt.base64encode(client_key_xor_sig)
	local client_final = crypt.base64encode(without_proof .. ',' .. client_proof)
	local server_key = crypt.hmac_sha1(salted_pass, "Server Key")
	local server_sig = crypt.base64encode(crypt.hmac_sha1(server_key, auth_msg))

	r = self:runCommand("saslContinue",1,"conversationId",conversationId,"payload",client_final)
	if r.ok ~= 1 then
		return false
	end
	parsed_s = crypt.base64decode(r['payload'])
	parsed_t = {}
	for k, v in string.gmatch(parsed_s, "(%w+)=([^,]*)") do
		parsed_t[k] = v
	end
	if parsed_t['v'] ~= server_sig then
		skynet.error("Server returned an invalid signature.")
		return false
	end
	if not r.done then
		r = self:runCommand("saslContinue",1,"conversationId",conversationId,"payload","")
		if r.ok ~= 1 then
			return false
		end
		if not r.done then
			skynet.error("SASL conversation failed to complete.")
			return false
		end
	end
	return true
end

function mongo_client:logout()
	local result = self:runCommand "logout"
	return result.ok ==	1
end

function mongo_db:auth(user, pass)
	local authmod = rawget(self.connection, "authmod") or "scram_sha1"
	local auth_func = auth_method["auth_" .. authmod]
	assert(auth_func , "Invalid authmod")
	return auth_func(self, user, pass)
end

function mongo_db:runCommand(cmd,cmd_v,...)
	local conn = self.connection
	local request_id = conn:genId()
	local sock = conn.__sock
	local bson_cmd
	if not cmd_v then
		-- ensure cmd remains in first place
		bson_cmd = bson_encode_order(cmd,1, "$db", self.name)
	else
		bson_cmd = bson_encode_order(cmd,cmd_v, "$db", self.name, ...)
	end

	local pack = driver.op_msg(request_id, 0, bson_cmd)
	-- we must hold	req	(req.data),	because	req.document is	a lightuserdata, it's a	pointer	to the string (req.data)
	local req =	sock:request(pack, request_id)
	local doc =	req.document
	return bson_decode(doc)
end

--- send command without response
function mongo_db:send_command(cmd, cmd_v, ...)
	local conn = self.connection
	local request_id = conn:genId()
	local sock = conn.__sock
	local bson_cmd
	if not cmd_v then
		-- ensure cmd remains in first place
		bson_cmd = bson_encode_order(cmd, 1, "$db", self.name, "writeConcern", {w=0})
	else
		bson_cmd = bson_encode_order(cmd, cmd_v, "$db", self.name, "writeConcern", {w=0}, ...)
	end

	local pack = driver.op_msg(request_id, 2, bson_cmd)
	sock:request(pack)
	return {ok=1} -- fake successful response
end

function mongo_db:getCollection(collection)
	local col =	{
		connection = self.connection,
		name = collection,
		full_name =	self.full_name .. "." .. collection,
		database = self.database,
	}
	self[collection] = setmetatable(col, collection_meta)
	return col
end

mongo_collection.getCollection = mongo_db.getCollection

function mongo_collection:insert(doc)
	if doc._id == nil then
		doc._id	= bson.objectid()
	end
	self.database:send_command("insert", self.name, "documents", {bson_encode(doc)})
end

local function werror(r)
	local ok = (r.ok == 1 and not r.writeErrors and not r.writeConcernError and not r.errmsg)

	local err
	if not ok then
		if r.writeErrors then
			err = r.writeErrors[1].errmsg
		elseif r.writeConcernError then
			err = r.writeConcernError.errmsg
		else
			err = r.errmsg
		end
	end
	return ok, err, r
end

function mongo_collection:safe_insert(doc)
	local r = self.database:runCommand("insert", self.name, "documents", {bson_encode(doc)})
	return werror(r)
end

function mongo_collection:raw_safe_insert(doc)
	local r = self.database:runCommand("insert", self.name, "documents", {doc})
	return werror(r)
end

function mongo_collection:batch_insert(docs)
	for	i=1,#docs do
		if docs[i]._id == nil then
			docs[i]._id	= bson.objectid()
		end
		docs[i]	= bson_encode(docs[i])
	end

	self.database:send_command("insert", self.name, "documents", docs)
end

mongo_collection.insert_many = mongo_collection.batch_insert

function mongo_collection:safe_batch_insert(docs)
	for i = 1, #docs do
		if docs[i]._id == nil then
			docs[i]._id = bson.objectid()
		end
		docs[i] = bson_encode(docs[i])
	end

	local r = self.database:runCommand("insert", self.name, "documents", docs)
	return werror(r)
end

mongo_collection.safe_insert_many = mongo_collection.safe_batch_insert

function mongo_collection:update(query,update,upsert,multi)
	self.database:send_command("update", self.name, "updates", {bson_encode({
		q = query,
		u = update,
		upsert = upsert,
		multi = multi
	})})
end

function mongo_collection:safe_update(query, update, upsert, multi)
	local r = self.database:runCommand("update", self.name, "updates", {bson_encode({
		q = query,
		u = update,
		upsert = upsert,
		multi = multi,
	})})
	return werror(r)
end

function mongo_collection:batch_update(updates)
	local updates_tb = {}
	for i = 1, #updates do
		updates_tb[i] = bson_encode({
			q = updates[i].query,
			u = updates[i].update,
			upsert = updates[i].upsert,
			multi = updates[i].multi,
		})
	end

	self.database:send_command("update", self.name, "updates", updates_tb)
end

function mongo_collection:safe_batch_update(updates)
	local updates_tb = {}
	for i = 1, #updates do
		updates_tb[i] = bson_encode({
			q = updates[i].query,
			u = updates[i].update,
			upsert = updates[i].upsert,
			multi = updates[i].multi,
		})
	end

	local r = self.database:runCommand("update", self.name, "updates", updates_tb)
	return werror(r)
end

function mongo_collection:raw_safe_update(update)
	local r = self.database:runCommand("update", self.name, "updates", {update})
	return werror(r)
end

function mongo_collection:delete(query, single)
	self.database:runCommand("delete", self.name, "deletes", {bson_encode({
		q = query,
		limit = single and 1 or 0,
	})})
end

function mongo_collection:safe_delete(query, single)
	local r = self.database:runCommand("delete", self.name, "deletes", {bson_encode({
		q = query,
		limit = single and 1 or 0,
	})})
	return werror(r)
end

function mongo_collection:safe_batch_delete(deletes, single)
	local delete_tb = {}
	for i = 1, #deletes do
		delete_tb[i] = bson_encode({
			q = deletes[i],
			limit = single and 1 or 0,
		})
	end
	local r = self.database:runCommand("delete", self.name, "deletes", delete_tb)
	return werror(r)
end

function mongo_collection:raw_safe_delete(delete)
	local r = self.database:runCommand("delete", self.name, "deletes", {delete})
	return werror(r)
end

function mongo_collection:findOne(query, projection)
	local cursor = self:find(query, projection)
	if cursor:hasNext() then
		return cursor:next()
	end
	return nil
end

function mongo_collection:find(query, projection)
	return setmetatable( {
		__collection = self,
		__query	= query	and	bson_encode(query) or empty_bson,
		__projection = projection and bson_encode(projection) or empty_bson,
		__ptr =	nil,
		__data = nil,
		__cursor = nil,
		__document = {},
		__flags	= 0,
		__skip = 0,
		__limit = 0,
		__sort = empty_bson,
	} ,	cursor_meta)
end

local function unfold(list, key, ...)
	if key == nil then
		return list
	end
	local next_func, t = pairs(key)
	local k, v = next_func(t)	-- The first key pair
	table.insert(list, k)
	table.insert(list, v)
	return unfold(list, ...)
end

-- cursor:sort { key = 1 } or cursor:sort( {key1 = 1}, {key2 = -1})
function mongo_cursor:sort(key, key_v, ...)
	if key_v then
		local key_list = unfold({}, key, key_v , ...)
		key = bson_encode_order(table.unpack(key_list))
	end
	self.__sort = key
	return self
end

function mongo_cursor:skip(amount)
	self.__skip = amount
	return self
end

function mongo_cursor:limit(amount)
	self.__limit = amount
	return self
end

function mongo_cursor:count(with_limit_and_skip)
	local cmd = {
		'count', self.__collection.name,
		'query', self.__query,
	}
	if with_limit_and_skip then
		local len = #cmd
		cmd[len+1] = 'limit'
		cmd[len+2] = self.__limit
		cmd[len+3] = 'skip'
		cmd[len+4] = self.__skip
	end
	local ret = self.__collection.database:runCommand(table.unpack(cmd))
	assert(ret and ret.ok == 1)
	return ret.n
end


-- For compatibility.
-- collection:createIndex({username = 1}, {unique = true})
local function createIndex_onekey(self, key, option)
	local doc = {}
	for k,v in pairs(option) do
		doc[k] = v
	end
	local k,v = next(key)	-- support only one key
	assert(next(key,k) == nil, "Use new api for multi-keys")
	doc.name = doc.name or (k .. "_" .. v)
	doc.key = key

	return self.database:runCommand("createIndexes", self.name, "indexes", {doc})
end


local function IndexModel(option)
	local doc = {}
	for k,v in pairs(option) do
		if type(k) == "string" then
			doc[k] = v
		end
	end

	local keys = {}
	local name
	for _, kv in ipairs(option) do
		local k,v
		if type(kv) == "string" then
			k = kv
			v = 1
		else
			k,v = next(kv)
		end
		table.insert(keys, k)
		table.insert(keys, v)
		name = (name == nil) and k or (name .. "_" .. k)
		name = name  .. "_" .. v
	end
	assert(name, "Need keys")

	doc.name = doc.name or name
	doc.key = bson_encode_order(table.unpack(keys))

	return doc
end

-- collection:createIndex { { key1 = 1}, { key2 = 1 },  unique = true }
-- or collection:createIndex { "key1", "key2",  unique = true }
-- or collection:createIndex( { key1 = 1} , { unique = true } )	-- For compatibility
function mongo_collection:createIndex(arg1 , arg2)
	if arg2 then
		return createIndex_onekey(self, arg1, arg2)
	else
		return self.database:runCommand("createIndexes", self.name, "indexes", { IndexModel(arg1) })
	end
end

function mongo_collection:createIndexes(...)
	local idx = { ... }
	for k,v in ipairs(idx) do
		idx[k] = IndexModel(v)
	end
	return self.database:runCommand("createIndexes", self.name, "indexes", idx)
end

mongo_collection.ensureIndex = mongo_collection.createIndex

function mongo_collection:drop()
	return self.database:runCommand("drop", self.name)
end

-- collection:dropIndex("age_1")
-- collection:dropIndex("*")
function mongo_collection:dropIndex(indexName)
	return self.database:runCommand("dropIndexes", self.name, "index", indexName)
end

-- collection:findAndModify({query = {name = "userid"}, update = {["$inc"] = {nextid = 1}}, })
-- keys, value type
-- query, table
-- sort, table
-- remove, bool
-- update, table
-- new, bool
-- fields, bool
-- upsert, boolean
function mongo_collection:findAndModify(doc)
	assert(doc.query)
	assert(doc.update or doc.remove)

	local cmd = {"findAndModify", self.name};
	for k, v in pairs(doc) do
		table.insert(cmd, k)
		table.insert(cmd, v)
	end
	return self.database:runCommand(table.unpack(cmd))
end

-- https://docs.mongodb.com/manual/reference/command/aggregate/
-- collection:aggregate({ { ["$project"] = {tags = 1} } }, {cursor={}})
-- @param pipeline: array
-- @param options: map
-- @return
function mongo_collection:aggregate(pipeline, options)
	assert(pipeline)
	local cmd = {"aggregate", self.name, "pipeline", pipeline}
	for k, v in pairs(options) do
		table.insert(cmd, k)
		table.insert(cmd, v)
	end
	return self.database:runCommand(table.unpack(cmd))
end

function mongo_cursor:hasNext()
	if self.__ptr == nil then
		if self.__document == nil then
			return false
		end
		local response

		local database = self.__collection.database
		if self.__data == nil then
			local name = self.__collection.name
			response = database:runCommand("find", name, "filter", self.__query, "sort", self.__sort,
				"skip", self.__skip, "limit", self.__limit, "projection", self.__projection)
		else
			if self.__cursor  and self.__cursor > 0 then
				local name = self.__collection.name
				response = database:runCommand("getMore", bson_int64(self.__cursor), "collection", name)
			else
				-- no more
				self.__document	= nil
				self.__data	= nil
				return false
			end
		end

		if response.ok ~= 1 then
			self.__document	= nil
			self.__data	= nil
			self.__cursor =	nil
			error(response["$err"] or "Reply from mongod error")
		end

		local cursor = response.cursor
		self.__document = cursor.firstBatch or cursor.nextBatch
		self.__data = response
		self.__ptr = 1
		self.__cursor = cursor.id

		local limit = self.__limit
		if cursor.id > 0 and limit > 0 then
			limit = limit - #self.__document
			if limit <= 0 then
				-- reach limit
				self:close()
			end

			self.__limit = limit
		end

		if cursor.id == 0 and #self.__document == 0 then -- nomore
			return false
		end

		return true
	end

	return true
end

function mongo_cursor:next()
	if self.__ptr == nil then
		error "Call	hasNext	first"
	end
	local r	= self.__document[self.__ptr]
	self.__ptr = self.__ptr	+ 1
	if self.__ptr >	#self.__document then
		self.__ptr = nil
	end

	return r
end

function mongo_cursor:close()
	if self.__cursor and self.__cursor > 0 then
		local coll = self.__collection
		coll.database:send_command("killCursors", coll.name, "cursors", {bson_int64(self.__cursor)})
		self.__cursor = nil
	end
end

return mongo
