local bson = require "bson"
local socket = require "socket"
local socketchannel = require "socketchannel"
local skynet = require "skynet"
local driver = require "mongo.driver"
local md5 = require "md5"
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
	-- DO NOT need disconnect, because channel will shutdown during gc
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

local function dispatch_reply(so)
	local len_reply = so:read(4)
	local reply = so:read(driver.length(len_reply))
	local result = { result = {} }
	local succ, reply_id, document, cursor_id, startfrom = driver.reply(reply, result.result)
	result.document = document
	result.cursor_id = cursor_id
	result.startfrom = startfrom
	result.data = reply
	return reply_id, succ, result
end

local function mongo_auth(mongoc)
	local user = rawget(mongoc, "username")
	local pass = rawget(mongoc, "password")

	if user == nil or pass == nil then
		return
	end
	return function()
		assert(mongoc:auth(user, pass))
	end
end

function mongo.client( conf )
	local obj = {
		host = conf.host,
		port = conf.port or 27017,
	}

	obj.__id = 0
	obj.__sock = socketchannel.channel {
		host = obj.host,
		port = obj.port,
		response = dispatch_reply,
		auth = mongo_auth(conf),
	}
	setmetatable(obj, client_meta)
	obj.__sock:connect(true)	-- try connect only once
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
		so:close()
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

function mongo_client:auth(user,password)
	local password = md5.sumhexa(string.format("%s:mongo:%s",user,password))
	local result= self:runCommand "getnonce"
	if result.ok ~=1 then
		return false
	end

	local key = md5.sumhexa(string.format("%s%s%s",result.nonce,user,password))
	local result= self:runCommand ("authenticate",1,"user",user,"nonce",result.nonce,"key",key)
	return result.ok == 1
end

function mongo_client:logout()
	local result = self:runCommand "logout"
	return result.ok == 1
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
	local doc = sock:request(pack, request_id).document
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
	-- flags support 1: ContinueOnError
	sock:request(pack)
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
	sock:request(pack)
end

function mongo_collection:update(selector,update,upsert,multi)
	local flags = (upsert and 1 or 0) + (multi and 2 or 0)
	local sock = self.connection.__sock
	local pack = driver.update(self.full_name, flags, bson_encode(selector), bson_encode(update))
	sock:request(pack)
end

function mongo_collection:delete(selector, single)
	local sock = self.connection.__sock
	local pack = driver.delete(self.full_name, single, bson_encode(selector))
	sock:request(pack)
end

function mongo_collection:findOne(query, selector)
	local conn = self.connection
	local request_id = conn:genId()
	local sock = conn.__sock
	local pack = driver.query(request_id, 0, self.full_name, 0, 1, query and bson_encode(query) or empty_bson, selector and bson_encode(selector))
	local doc = sock:request(pack, request_id).document
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

		local ok, result = pcall(sock.request,sock,pack, request_id)

		local doc = result.document
		local cursor = result.cursor_id

		if ok then
			if doc then
				self.__document = result.result
				self.__data = result.data
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
		sock:request(pack)
	end
end

return mongo
