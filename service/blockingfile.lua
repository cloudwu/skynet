local skynet = require "skynet"

local max_threads = ...
max_threads = tonumber(max_threads) or 4
local total_threads = tonumber(skynet.getenv("thread"))
if max_threads >= total_threads then
	max_threads = total_threads - 1
end
if max_threads <= 0 then
	max_threads = 1
end

local agents = {}		-- {str_addr = {addr = num, count = num}}
local associate = {}	-- {[file_handle_str] = {addr = num, start = num, filename = str}}

-- 创建n个agent
local function create_blocking_agents(n)
	for i = 1, n do
		local addr = skynet.newservice("blockingfile_agent")
		local name = tostring(addr)
		agents[name] = {addr = addr, count = 0}
	end
end

-- 选择打开文件数量最少的agent
local function select_agent()
	local min
	for k, agent in pairs(agents) do
		if not min then
			min = agent
		else
			if min.count > agent.count then
				min = agent
			end
		end
	end
	return min
end

local function print_agents()
	local arr = {}
	for k, v in pairs(agents) do
		table.insert(arr, string.format("%s:%d", v.addr, v.count))
	end

	print("============= agent stat: ", table.concat(arr, "  "))
end

local function do_call(cb, handle)
	local r = associate[handle]	-- {addr = num, start = num, filename = str}
	if not r then
		-- handle存在
		return skynet.retpack(nil, "bad handler!")
	end

	local res = {cb(r.addr)}
	return table.unpack(res)
end

--　文件打开时间太长告警
local warning_open_time = 10*100
local function check()
	local now = skynet.now()
	for _, assoc in pairs(associate) do
		local d = now - assoc.start
		if d > warning_open_time then
			local msg = string.format("WARNING!! still open: filename:%s, agent:%s, diff:%ds", assoc.filename, assoc.addr, d/100)
			skynet.error(msg)
		end
	end
end

local function dispatch(session, address, cmd, ...)
	if cmd == "open" then
		local agent = select_agent()	-- 选择agent
		local handle, err = skynet.call(agent.addr, "lua", "open", ...)
		if not err then
			local filename = ...

			-- 缓存handle与agent的信息
			associate[handle] = {addr = agent.addr, start = skynet.now(), filename = filename}
			agent.count = agent.count + 1
		end

		return skynet.retpack(handle, err)
	end

	local args = {...}
	local handle = args[1]
	if cmd == "read" or cmd == "seek" or cmd == "write" or cmd == "flush" then
		--　查找handle对应的agent,并调用
		return do_call(function(addr)
			return skynet.retpack(skynet.call(addr, "lua", cmd, table.unpack(args)))
		end, handle)
	end

	if cmd == "close" then
		return do_call(function(agent_addr)
			-- 取消关联
			local r = associate[handle]	-- {addr = num, start = num, filename = str}
			associate[handle] = nil

			local agent = agents[tostring(r.addr)]
			agent.count = agent.count - 1
			assert(agent.count >= 0)

			-- 调用
			return skynet.retpack(skynet.call(agent_addr, "lua", "close", table.unpack(args)))
		end, handle)
	end

	error(string.format("Unknown blocking %s", tostring(cmd)))
end

skynet.start(function()
	skynet.error("start blocking files, max threads:", max_threads)

	create_blocking_agents(max_threads)

	skynet.dispatch("lua", dispatch)

	-- 关闭文件检测
	skynet.fork(function()
		while true do
			skynet.sleep(500)
			check()
		end
	end)
end)
