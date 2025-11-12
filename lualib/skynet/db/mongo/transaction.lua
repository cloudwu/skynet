--[[
mongo会话事务接口支持子模块，可根据需要引入：
local mongo = require "mongo"
mongo.enable("transaction")
]]
local bson = require "bson"
local driver = require "skynet.mongo.driver"
local assert = assert
local table = table
local mongo = require "mongo"

local bson_encode =	bson.encode
local bson_encode_order	= bson.encode_order
local bson_decode =	bson.decode
local bson_int64 = bson.int64
local empty_bson = bson_encode {}

local M = {}

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

function M.init(depend)
	--[[
	要使用mongodb事务的前提是需要先将mongodb配置为副本集，否则报错：Transaction numbers are only allowed on a replica set member or mongos
	比如修改配置文件/etc/mongod.conf中：
	 replication:
	   replSetName: "rs0"
	然后重启monogod服务
	以下带下划线结尾的接口为内部私有接口，目前不宜应用层直接调用
	]]
	-- {
	--   id = {
	--     id = "000504301FB085921E4FF58C38E3F270922FFF",
	--   },
	--   timeoutMinutes = 30,
	--   ok = 1.0,
	-- }
	local mongo_db = depend.mongo_db
	assert("table" == type(mongo_db))
	---@return table
	function mongo_db:startSession_()
		return self:runCommand("startSession") -- 由mongo服务器生成，避免自产生的id相同的冲突
	end

	---@param session_id table
	function mongo_db:endSessions_(session_id)
		return self:runCommand("endSessions", {bson_encode(session_id)})
	end

	local transaction_id = 0
	---@return integer
	local function startTransaction_()
		transaction_id = transaction_id + 1
		if transaction_id > 9999999 then
			transaction_id = 1
		end
		return transaction_id
	end

	---@param session_id table
	---@param transaction_id integer
	local function genTransactionParams(session_id, transaction_id, startTransaction)
		return "lsid", bson_encode(session_id),
			"txnNumber", bson_int64(transaction_id)       -- 这是此会话中的第一个事务，故从1开始 -- BSON field 'OperationSessionInfo.txnNumber' is the wrong type 'int', expected type 'long'
			,"autocommit", false
			, startTransaction and "startTransaction" or nil, startTransaction and startTransaction or nil
	end
	-- 原始裸接口备用，考虑是否需要开放支持跨协程事务，但每个更新操作需手动额外调用runCommand来辅助实现，而不能调用原有相关接口(如safe_insert)
	-- ---@param session_id table
	-- ---@param transaction_id integer
	-- function mongo_client:commitTransaction_(session_id, transaction_id) -- commitTransaction may only be run against the admin database.
	-- 	return self:runCommand("commitTransaction", 1, "lsid", bson_encode(session_id), "txnNumber", bson_int64(transaction_id), "autocommit", false)
	-- end
	-- ---@param session_id table
	-- ---@param transaction_id integer
	-- function mongo_client:abortTransaction_(session_id, transaction_id)
	-- 	return self:runCommand("abortTransaction", 1, "lsid", bson_encode(session_id), "txnNumber", bson_int64(transaction_id), "autocommit", false) -- 如果不设置autocommit则报错：txnNumber may only be provided for multi-document transactions and retryable write commands. autocommit:false was not provided, and abortTransaction is not a retryable write command.
	-- end
	local cur_coroutine = coroutine.running
	local transaction = {}
	local function is_in_transaction()
		return transaction[cur_coroutine()]
	end
	local function get_cur_transaction_params()
		local TransactionParams = transaction[cur_coroutine()]
		assert(TransactionParams)
		return table.unpack(TransactionParams)
	end
	local function set_transaction_params(params)
		local TransactionParams = transaction[cur_coroutine()]
		assert(TransactionParams)
		table.move(TransactionParams, 1, #TransactionParams, #params + 1, params)
		-- startTransaction只设置一次为true
		while #TransactionParams > 6 do
			table.remove(TransactionParams)
		end
		-- table.insert(params, "writeConcern") -- writeConcern is not allowed within a multi-statement transaction
		-- table.insert(params, bson_encode({w=0}))
		return params
	end

	local debug_traceback = debug.traceback
	-- 只支持同协程事务接口，兼容老接口
	---@param fun_transaction fun() 包含多个更新语句，这些语句要么全部成功，要么全部取消，目前同时支持run和send机制，但send接口会自动去掉writeConcern选项
	---@return any failed error info | nil ok
	function mongo_db:run_transaction(fun_transaction)
		local ret = self:startSession_()
		transaction[cur_coroutine()] = {genTransactionParams(ret.id, startTransaction_(), true)}
		local ok, err = xpcall(fun_transaction, debug_traceback)
		local r
		if ok then
			r = self:commit_transaction_() -- 不能报异常，否则后续transaction无法清理
		else
			r = self:abort_transaction_() -- 不能报异常，否则后续transaction无法清理
			-- error(err, 0)
		end
		transaction[cur_coroutine()] = nil -- 这个位置很关键，必须在commit_transaction和abort_transaction之后，因为其仍依赖本数据
		self:endSessions_(ret.id) -- 这个不是本事务的关键，所以不处理其返回结果
		if not err then
			local _
			_, err = werror(r)
		end
		return err -- 如果成功err应该为nil
	end

	function mongo_db:commit_transaction_()
		-- local TransactionParams = transaction[cur_coroutine()]
		-- assert(TransactionParams)
		-- transaction[cur_coroutine()] = nil
		-- return self.connection:runCommand("commitTransaction", 1, table.unpack(TransactionParams))
		local ok, r = pcall(function()
			return self.connection:runCommand("commitTransaction")
		end)
		-- transaction[cur_coroutine()] = nil
		if not ok then
			r = { errmsg = r } -- 模拟mongo风格错误
		end
		return r
	end

	function mongo_db:abort_transaction_()
		-- local TransactionParams = transaction[cur_coroutine()]
		-- assert(TransactionParams)
		-- transaction[cur_coroutine()] = nil
		-- return self.connection:runCommand("abortTransaction", 1, table.unpack(TransactionParams))
		local ok, r = pcall(function()
			return self.connection:runCommand("abortTransaction")
		end)
		-- transaction[cur_coroutine()] = nil
		if not ok then
			r = { errmsg = r } -- 模拟mongo风格错误
		end
		return r
	end
	local old_runCommand = mongo_db.runCommand
	local old_send_command = mongo_db.send_command
	function mongo_db:runCommand(cmd,cmd_v,...)
		if is_in_transaction() then
			cmd_v = cmd_v or 1
			local params = {cmd,cmd_v,...}
			return old_runCommand(self, table.unpack(set_transaction_params(params)))
		end
		return old_runCommand(self, cmd,cmd_v,...)
	end
	function mongo_db:send_command(cmd,cmd_v,...)
		if is_in_transaction() then
			cmd_v = cmd_v or 1
			local params = {...} -- {cmd,cmd_v,...} 这里要去掉下面重复的cmd和cmd_v
			-- return old_send_command(self, table.unpack(set_transaction_params(params)))
			-- 这里我要需要将writeConcern选项去掉，因为MongoDB 不允许在事务中使用 writeConcern
			local conn = self.connection
			local request_id = conn:genId()
			local sock = conn.__sock
			local bson_cmd
			if not cmd_v then
				-- ensure cmd remains in first place
				bson_cmd = bson_encode_order(cmd, 1, "$db", self.name, table.unpack(set_transaction_params(params)))
			else
				bson_cmd = bson_encode_order(cmd, cmd_v, "$db", self.name, table.unpack(set_transaction_params(params)))
			end

			local pack = driver.op_msg(request_id, 2, bson_cmd)
			sock:request(pack)
			return {ok=1} -- fake successful response
		end
		return old_send_command(self, cmd,cmd_v,...)
	end

	-- 测试及使用示例
	function mongo_db:test_transaction_()
		local tb_name = "tb_test"
		self[tb_name]:drop() -- Transaction numbers are only allowed on a replica set member or mongos
		local db = self[tb_name]
		print(self:run_transaction(function()
			print("-----------------------1")
			print(db:safe_insert({_id = bson.objectid(), key = 1, d = 1})) -- Only servers in a sharded cluster can start a new transaction at the active transaction number
			print("-----------------------2")
			print(db:safe_insert({_id = bson.objectid(), key = 2, d = 2}))
			print(db:insert({_id = bson.objectid(), key = 4, d = 4, by = "send"})) -- 测试send_command接口混用
			-- error("err") -- 如果报错，上面的更新将自动回滚
			print("-----------------------3")
			print(db:safe_insert({_id = bson.objectid(), key = 3, d = 3}))
		end))
	end
end

return M