local bson = require "bson"
local socket = require "socket"
local skynet = require "skynet"
local driver = require "mongo.driver"
local rawget = rawget
local assert = assert

local bson_encode = bson.encode
local bson_encode_order = bson.encode_order
local bson_decode = bson.decode
local empty_bson = bson_encode {}

local mongo = {}
mongo.null = assert(bson.null)
mongo.maxkey = assert(bson.maxkey)
mongo.minkey = assert(bson.minkey)
mongo.type = assert(bson.type)

local mongo_cursor = {}
local cursor_meta = {
	__index = mongo_cursor,
}

local mongo_client = {}

local client_meta = {
	__index = function(self, key)
		return rawget(mongo_client, key) or self:getDB(key)
	end,
	__tostring = function (self)
		local port_string
		if self.port then
			port_string = ":" .. tostring(self.port)
		else
			port_string = ""
		end

		return "[mongo client : " .. self.host .. port_string .."]"
	end,
	__gc = function(self)
		self:disconnect()
	end
}

local mongo_db = {}

local db_meta = {
	__index = function (self, key)
		return rawget(mongo_db, key) or self:getCollection(key)
	end,
	__tostring = function (self)
		return "[mongo db : " .. self.name .. "]"
	end
}

local mongo_collection = {}
local collection_meta = {
	__index = function(self, key)
		return rawget(mongo_collection, key) or self:getCollection(key)
	end ,
	__tostring = function (self)
		return "[mongo collection : " .. self.full_name .. "]"
	end
}

local function try_connect(host, port)
	-- try 10 times
	for i = 1, 10 do
		local sock = socket.open(host, port)
		if not sock then
			-- todo: write log
			print("Try to connect " .. host .. " failed")
			skynet.sleep(100*i)
		else
			return sock
		end
	end
	error("Connect to mongo " .. host .. " failed")
end

local function reconnect(obj)
	for _,v in pairs(obj.__request) do
		v.succ = nil
		-- wakeup request coroutine and send a failure.
		skynet.wakeup(v.co)
	end
	-- todo: reconnect is too simple now.
	local sock = try_connect(obj.host, obj.port)
	obj.__sock = sock
	return sock
end

local function reply_queue(obj)
	local sock = obj.__sock
	local tmp = {}
	local request_set = obj.__request
	while true do
		local len_reply = socket.read(sock, 4)
		if not len_reply then
			if not obj.__sock then
				return
			end
			sock = reconnect(obj)
		else
			local length = driver.length(len_reply)
			local reply = socket.read(sock, length)
			if not reply then
				if not obj.__sock then
					return
				end
				sock = reconnect(obj)
			else
				local succ, reply_id, document, cursor_id, startfrom = driver.reply(reply, tmp)
				local result = assert(request_set[reply_id])
				driver.copy_result(tmp, result.result)
				result.succ = succ
				result.document = document
				result.cursor_id = cursor_id
				result.startfrom = startfrom
				result.data = reply
				skynet.wakeup(result.co)
			end
		end
	end
end

function mongo.client( obj )
	obj.port = obj.port or 27017
	obj.__id = 0
	obj.__sock = try_connect(obj.host, obj.port)
	obj.__request = {}
	setmetatable(obj, client_meta)
	skynet.fork(reply_queue, obj)
	return obj
end

function mongo_client:getDB(dbname)
	local db = {
		connection = self,
		name = dbname,
		full_name = dbname,
		database = false,
		__cmd = dbname .. "." .. "$cmd",
	}
	db.database = db

	return setmetatable(db, db_meta)
end

function mongo_client:disconnect()
	if self.__sock then
		local so = self.__sock
		self.__sock = false
		socket.close(so)
	end
end

function mongo_client:genId()
	local id = self.__id + 1
	self.__id = id
	return id
end

function mongo_client:runCommand(...)
	if not self.admin then
		self.admin = self:getDB "admin"
	end
	return self.admin:runCommand(...)
end

local function get_reply(conn, request_id, result)
	local r = { result = result , co = coroutine.running() }
	conn.__request[request_id] = r
	skynet.wait()
	conn.__request[request_id] = nil
	return r.data, r.succ, r.document, r.cursor_id, r.startfrom
end

function mongo_db:runCommand(cmd,cmd_v,...)
	local conn = self.connection
	local request_id = conn:genId()
	local sock = conn.__sock
	local bson_cmd
	if not cmd_v then
		bson_cmd = bson_encode_order(cmd,1)
	else
		bson_cmd = bson_encode_order(cmd,cmd_v,...)
	end
	local pack = driver.query(request_id, 0, self.__cmd, 0, 1, bson_cmd)
	-- todo: check send
	assert(socket.write(sock, pack), "write fail")
	local _, succ, doc = get_reply(conn,request_id)
	-- todo: check succ and doc
	assert(succ, "runCommand error")
	return bson_decode(doc)
end

function mongo_db:getCollection(collection)
	local col = {
		connection = self.connection,
		name = collection,
		full_name = self.full_name .. "." .. collection,
		database = self.database,
	}
	self[collection] = setmetatable(col, collection_meta)
	return col
end

mongo_collection.getCollection = mongo_db.getCollection

function mongo_collection:insert(doc)
	if doc._id == nil then
		doc._id = bson.objectid()
	end
	local sock = self.connection.__sock
	local pack = driver.insert(0, self.full_name, bson_encode(doc))
	-- todo: check send
	-- flags support 1: ContinueOnError
	assert(socket.write(sock, pack), "write fail")
end

function mongo_collection:batch_insert(docs)
	for i=1,#docs do
		if docs[i]._id == nil then
			docs[i]._id = bson.objectid()
		end
		docs[i] = bson_encode(docs[i])
	end
	local sock = self.connection.__sock
	local pack = driver.insert(0, self.full_name, docs)
	-- todo: check send
	assert(socket.write(sock, pack), "write fail")
end

function mongo_collection:update(selector,update,upsert,multi)
	local flags = (upsert and 1 or 0) + (multi and 2 or 0)
	local sock = self.connection.__sock
	local pack = driver.update(self.full_name, flags, bson_encode(selector), bson_encode(update))
	-- todo: check send
	assert(socket.write(sock, pack),"write fail")
end

function mongo_collection:delete(selector, single)
	local sock = self.connection.__sock
	local pack = driver.delete(self.full_name, single, bson_encode(selector))
	-- todo: check send
	assert(socket.write(sock, pack), "write fail")
end

function mongo_collection:findOne(query, selector)
	local conn = self.connection
	local request_id = conn:genId()
	local sock = conn.__sock
	local pack = driver.query(request_id, 0, self.full_name, 0, 1, query and bson_encode(query) or empty_bson, selector and bson_encode(selector))

	-- todo: check send
	assert(socket.write(sock, pack),"write fail")

	local _, succ, doc = get_reply(conn, request_id)
	-- todo: check succ
	return bson_decode(doc)
end

function mongo_collection:find(query, selector)
	return setmetatable( {
		__collection = self,
		__query = query and bson_encode(query) or empty_bson,
		__selector = selector and bson_encode(selector),
		__ptr = nil,
		__data = nil,
		__cursor = nil,
		__document = {},
		__flags = 0,
	} , cursor_meta)
end

function mongo_cursor:hasNext()
	if self.__ptr == nil then
		if self.__document == nil then
			return false
		end
		local conn = self.__collection.connection
		local request_id = conn:genId()
		local sock = conn.__sock
		local pack
		if self.__data == nil then
			pack = driver.query(request_id, self.__flags, self.__collection.full_name,0,0,self.__query,self.__selector)
		else
			if self.__cursor then
				pack = driver.more(request_id, self.__collection.full_name,0,self.__cursor)
			else
				-- no more
				self.__document = nil
				self.__data = nil
				return false
			end
		end

		--todo: check send
		assert(socket.write(sock, pack),"write fail")

		local data, succ, doc, cursor = get_reply(conn, request_id, self.__document)
		if succ then
			if doc then
				self.__data = data
				self.__ptr = 1
				self.__cursor = cursor
				return true
			else
				self.__document = nil
				self.__data = nil
				self.__cursor = nil
				return false
			end
		else
			self.__document = nil
			self.__data = nil
			self.__cursor = nil
			if doc then
				local err = bson_decode(doc)
				error(err["$err"])
			else
				error("Reply from mongod error")
			end
		end
	end

	return true
end

function mongo_cursor:next()
	if self.__ptr == nil then
		error "Call hasNext first"
	end
	local r = bson_decode(self.__document[self.__ptr])
	self.__ptr = self.__ptr + 1
	if self.__ptr > #self.__document then
		self.__ptr = nil
	end

	return r
end

function mongo_cursor:close()
	-- todo: warning hasNext after close
	if self.__cursor then
		local sock = self.__collection.connection.__sock
		local pack = driver.kill(self.__cursor)
		-- todo: check send
		assert(socket.write(sock, pack),"write fail")
	end
end

return mongo