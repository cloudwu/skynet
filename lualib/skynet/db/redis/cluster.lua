-- a simple redis-cluster client
-- rewrite from https://github.com/antirez/redis-rb-cluster

local skynet = require "skynet"
local redis = require "skynet.db.redis"
local crc16 = require "skynet.db.redis.crc16"

local RedisClusterHashSlots = 16384
local RedisClusterRequestTTL = 16

local sync = {
	once = {
		tasks = {},
	}
}

function sync.once.Do(id,func,...)
	local tasks = sync.once.tasks
	local task = tasks[id]
	if not task then
		task = {
			waiting = {},
			result = nil,
		}
		tasks[id] = task
		local rettbl = table.pack(xpcall(func,debug.traceback,...))
		task.result = rettbl
		local waiting = task.waiting
		tasks[id] = nil
		if next(waiting) then
			for i,co in ipairs(waiting) do
				skynet.wakeup(co)
			end
		end
		return table.unpack(task.result,1,task.result.n)
	else
		local co = coroutine.running()
		table.insert(task.waiting,co)
		skynet.wait(co)
		return table.unpack(task.result,1,task.result.n)
	end
end


local _M = {}

local rediscluster = {}
rediscluster.__index = rediscluster

function _M.new(startup_nodes,opt,onmessage)
	if #startup_nodes == 0 then
		startup_nodes = {startup_nodes,}
	end
	opt = opt or {}
	local self = {
		startup_nodes = startup_nodes,
		max_connections = opt.max_connections or 256,
		connections = {},
		opt = opt,
		refresh_table_asap = false,

		-- for subscribe/publish
		__onmessage = onmessage,
		__watching = {},
	}
	setmetatable(self,rediscluster)
	self:initialize_slots_cache()
	return self
end

local function nodename(node)
	return string.format("%s:%d",node.host,node.port)
end

function rediscluster:get_redis_link(node)
	local conf = {
		host = node.host,
		port = node.port,
		auth = self.opt.auth,
		db = self.opt.db or 0,
	}
	return redis.connect(conf)
end
 
-- Given a node (that is just a Ruby hash) give it a name just
-- concatenating the host and port. We use the node name as a key
-- to cache connections to that node.
function rediscluster:set_node_name(node)
	if not node.name then
		node.name = nodename(node)
	end
end

-- Contact the startup nodes and try to fetch the hash slots -> instances
-- map in order to initialize the @slots hash.
function rediscluster:initialize_slots_cache()
	self.slots = {}
	self.nodes = {}
	for _,startup_node in ipairs(self.startup_nodes) do
		local ok = pcall(function ()
			local conn = self:get_connection(startup_node)
			local list = conn:cluster("slots")
			for _,result in ipairs(list) do
				local ip,port = table.unpack(result[3])
				assert(ip)
				port = assert(tonumber(port))
				local master_node = {
					host = ip,
					port = port,
					slaves = {},
				}
				self:set_node_name(master_node)
				for i=4,#result do
					local ip,port = table.unpack(result[i])
					assert(ip)
					port = assert(tonumber(port))
					local slave_node = {
						host = ip,
						port = port,
					}
					self:set_node_name(slave_node)
					table.insert(master_node.slaves,slave_node)
				end
				for slot=tonumber(result[1]),tonumber(result[2]) do
					table.insert(self.nodes,master_node)
					self.slots[slot] = master_node
				end
			end
			self.refresh_table_asap = false
		end)
		-- Exit the loop as long as the first node replies
		if ok then
			break
		end
	end
end

-- Flush the cache, mostly useful for debugging when we want to force
-- redirection.
function rediscluster:flush_slots_cache()
	self.slots = {}
end

-- Return the hash slot from the key.
function rediscluster:keyslot(key)
	-- Only hash what is inside {...} if there is such a pattern in the key.
	-- Note that the specification requires the content that is between
	-- the first { and the first } after the first {. If we found {} without
	-- nothing in the middle, the whole key is hashed as usually.
	local startpos = string.find(key,"{",1,true)
	if startpos then
		local endpos = string.find(key,"}",startpos+1,true)
		if endpos and endpos ~= startpos + 1 then
			key = string.sub(key,startpos+1,endpos-1)
		end
	end
	return crc16(key) % RedisClusterHashSlots
end

-- Return the first key in the command arguments.
--
-- Currently we just return argv[1], that is, the first argument
-- after the command name.
--
-- This is indeed the key for most commands, and when it is not true
-- the cluster redirection will point us to the right node anyway.
--
-- For commands we want to explicitly bad as they don't make sense
-- in the context of cluster, nil is returned.
function rediscluster:get_key_from_command(argv)
	local cmd,key = table.unpack(argv)
	cmd = string.lower(cmd)
	if cmd == "info" or
		cmd == "multi" or
		cmd == "exec" or
		cmd == "slaveof" or
		cmd == "config" or
		cmd == "shutdown" then
		return nil
	end
	if cmd == "eval" or cmd == "evalsha" then
		-- eval script numkeys key [key...] arg [arg...]
		local numkeys = argv[3]
		local firstkey_slot = nil
		for i=4,4+numkeys-1 do
			local slot = self:keyslot(argv[i])
			if not firstkey_slot then
				firstkey_slot = slot
			elseif firstkey_slot ~= slot then
				error(string.format("%s - all keys must map to the same key slot",cmd))
			end
		end
		-- numkeys <=0 will return nil
		return argv[4]
	end
	-- Unknown commands, and all the commands having the key
	-- as first argument are handled here:
	-- set, get, ...
	return key
end

-- If the current number of connections is already the maximum number
-- allowed, close a random connection. This should be called every time
-- we cache a new connection in the @connections hash.
function rediscluster:close_existing_connection()
	local length = 0
	for name,conn in pairs(self.connections) do
		length = length + 1
	end
	if length >= self.max_connections then
		pcall(function ()
			local name,conn = next(self.connections)
			self.connections[name] = nil
			conn:disconnect()
		end)
	end
end

function rediscluster:close_all_connection()
	local connections = self.connections
	self.connections = {}
	for name,conn in pairs(connections) do
		pcall(conn.disconnect,conn)
	end
end

function rediscluster:get_connection(node)
	if not node.name then
		node.name = nodename(node)
	end
	local name = node.name
	local ok,conn = sync.once.Do(name,function ()
		local conn = self.connections[name]
		if not conn then
			self:close_existing_connection()
			conn = self:get_redis_link(node)
			self.connections[name] = conn
		end
		return conn
	end)
	assert(ok,conn)
	return conn
end

-- Return a link to a random node, or raise an error if no node can be
-- contacted. This function is only called when we can't reach the node
-- associated with a given hash slot, or when we don't know the right
-- mapping.
-- The function will try to get a successful reply to the PING command,
-- otherwise the next node is tried.
function rediscluster:get_random_connection()
	-- shuffle
	local shuffle_idx = {}
	local startpos = 1
	local endpos = #self.nodes
	for i=startpos,endpos do
		shuffle_idx[i] = i
	end
	for i=startpos,endpos do
		local idx = math.random(i,endpos)
		local tmp = shuffle_idx[i]
		shuffle_idx[i] = shuffle_idx[idx]
		shuffle_idx[idx] = tmp
	end
	for i,idx in ipairs(shuffle_idx) do
		local ok,conn = pcall(function ()
			local node = self.nodes[idx]
			local conn = self:get_connection(node)
			if conn:ping() == "PONG" then
				return conn
			else
				-- If the connection is not good close it ASAP in order
				-- to avoid waiting for the GC finalizer. File
				-- descriptors are a rare resource.
				self.connetions[node.name] = nil
				conn:disconnect()
			end
		end)
		if ok and conn then
			return conn
		end
	end
	error("Can't reach a single startup node.")
end

-- Given a slot return the link (Redis instance) to the mapped node.
-- Make sure to create a connection with the node if we don't have
-- one.
function rediscluster:get_connection_by_slot(slot)
	local node = self.slots[slot]
	-- If we don't know what the mapping is, return a random node.
	if not node then
		return self:get_random_connection()
	end
	local ok,conn = pcall(self.get_connection,self,node)
	if not ok then
		if self.opt.read_slave and node.slaves and #node.slaves > 0 then
			local slave_node = node.slaves[math.random(1,#node.slaves)]
			local ok2,conn = pcall(self.get_connection,self,slave_node)
			if ok2 then
				conn:readonly()		-- allow this connection read-slave
				return conn
			end
		end
		-- This will probably never happen with recent redis-rb
		-- versions because the connection is enstablished in a lazy
		-- way only when a command is called. However it is wise to
		-- handle an instance creation error of some kind.
		return self:get_random_connection()
	end
	return conn
end

-- Dispatch commands.
function rediscluster:call(...)
	local argv = table.pack(...)
	local cmd = argv[1]
	local key = self:get_key_from_command(argv)
	if not key then
		error("No way to dispatch this command to Redis Cluster: " .. tostring(argv[1]))
	end
	if cmd == "subscribe" or cmd == "unsubscribe" or cmd == "psubscribe" or cmd == "punsubscribe" then
		local slot = self:keyslot(key)
		local conn = self:get_watch_connection_by_slot(slot)
		local func = conn[cmd]
		return func(conn,table.unpack(argv,2))
	end

	if self.refresh_table_asap then
		self:initialize_slots_cache()
	end
	local ttl = RedisClusterRequestTTL	-- Max number of redirections
	local err
	local asking = false
	local try_random_node = false
	while ttl > 0 do
		ttl = ttl - 1
		local conn
		local slot = self:keyslot(key)
		if asking then
			conn = self:get_connection(asking)
		elseif try_random_node then
			conn = self:get_random_connection()
			try_random_node = false
		else
			conn = self:get_connection_by_slot(slot)
		end
		local result = {pcall(function ()
			-- TODO: use pipelining to send asking and save a rtt.
			if asking then
				conn:asking()
			end
			asking = false
			local func = conn[cmd]
			return func(conn,table.unpack(argv,2))
		end)}
		local ok = result[1]
		if not ok then
			err = table.unpack(result,2)
			err = tostring(err)
			if err == "[Error: socket]" then
				-- may be never come here?
				try_random_node = true
				if ttl < RedisClusterRequestTTL/2 then
					skynet.sleep(10)
				end
			else
				-- err: ./lualib/skynet/socketchannel.lua:371: xxx
				err = string.match(err,".+:%d+:%s(.*)$") or err
				local errlist = {}
				for e in string.gmatch(err,"([^%s]+)%s?") do
					table.insert(errlist,e)
				end
				if (errlist[1] ~= "MOVED" and errlist[1] ~= "ASK") then
					error(err)
				else
					if errlist[1] == "ASK" then
						asking = true
					else
						-- Serve replied with MOVED. It's better for us to
						-- ask for CLUSTER SLOTS the next time.
						self.refresh_table_asap = true
					end
					local newslot = tonumber(errlist[2])
					local node_ip,node_port = string.match(errlist[3],"^([^:]+):([^:]+)$")
					node_port = assert(tonumber(node_port))
					local node = {
						host = node_ip,
						port = node_port,
					}
					if not asking then
						local oldnode = self.slots[newslot]
						if oldnode then
							node.slaves = oldnode.slaves
						end
						self:set_node_name(node)
						self.slots[newslot] = node
					else
						asking = node
					end
				end
			end
		else
			return table.unpack(result,2)
		end
	end
	error(string.format("Too many Cluster redirections?,maybe node is disconnected (last error: %q)",err))
end

function rediscluster:get_watch_connection_by_slot(slot)
	if not self.slots[slot] then
		self:initialize_slots_cache()
	end
	local node = assert(self.slots[slot])
	return self:get_watch_connection(node)
end

function rediscluster:get_watch_connection(node)
	local name = node.name
	local ok,conn = sync.once.Do(name,function ()
		if not self.__watching[name] then
			local conf = {
				host = node.host,
				port = node.port,
				auth = self.opt.auth,
				db = self.opt.db or 0,
			}
			local db = redis.watch(conf)
			self.__watching[name] = db
			local onmessage = rawget(self,"__onmessage")
			skynet.fork(function ()
				while true do
					local ok,data,channel,pchannel = pcall(db.message,db)
					if ok then
						if data and onmessage then
							pcall(onmessage,data,channel,pchannel)
						end
					end
				end
			end)
		end
		return self.__watching[name]
	end)
	assert(ok,conn)
	return conn
end

-- Currently we handle all the commands using method_missing for
-- simplicity. For a Cluster client actually it will be better to have
-- every single command as a method with the right arity and possibly
-- additional checks (example: RPOPLPUSH with same src/dst key, SORT
-- without GET or BY, and so forth).
setmetatable(rediscluster,{
	__index = function (t,cmd)
		t[cmd] = function (self,...)
			return self:call(cmd,...)
		end
		return t[cmd]
	end,
})

return _M
