local skynet = require "skynet"
local coroutine = require "skynet.coroutine"

local max_threads = ...
max_threads = tonumber(max_threads) or 4
local total_threads = tonumber(skynet.getenv("thread"))
if max_threads >= total_threads then
	max_threads = total_threads - 1
end
if max_threads <= 0 then
	max_threads = 1
end


local idle_count = 0
local running_count = 0
local idle_agents = {}		-- 空闲的服务
local running_agents = {}	-- 运行中的服务

local wait_queue = {}		-- 等待队列

local function create_blocking_agents(n)
	for i = 1, n do
		local addr = skynet.newservice("blocking_agent")
		local name = tostring(addr)
		idle_agents[name], running_agents[name] = addr, nil
	end
	idle_count, running_count = n, 0
end

local function try_get_idle()
	for name, addr in pairs(idle_agents) do
		-- move from idle to running
		running_agents[name], idle_agents[name] = addr, nil
		idle_count, running_count = idle_count - 1, running_count + 1
		assert(running_count >= 0 and idle_count >= 0)

		return addr
	end
	return nil
end

local function get_idle_service(timeout)
	local addr = try_get_idle()
	if addr then
		return addr
	end

	-- 找不到空闲的agent，加入等待队列
	local co = assert(coroutine.running())
	table.insert(wait_queue, co)

	local abandon = false
	timeout = tonumber(timeout)
	if timeout and timeout > 0 then
		-- 最长等待timeout*0.01s
		skynet.timeout(timeout, function()
			if not co then return end	-- 已经在超时前获取到agent

			abandon = true				-- 放弃获取agent

			-- 查找并 wakeup co
			for i, t in ipairs(wait_queue) do
				if co == t then
					skynet.wakeup(co)	-- 已经设置abandon=true, wakeup后不会再尝试获取agent
					table.remove(wait_queue, i)
					return
				end
			end

			assert(false)
		end)
	end

	skynet.wait()	-- 等待timeout或者release_agent唤醒

	if abandon then
		-- timeout唤醒,不再获取agent
		return nil, "timeout"
	end

	-- release_agent唤醒, 有空闲的agent可以获取了
	co = nil	-- release_agent先于timeout来到,设置co=nil,通知timeout已经获取到了

	return assert(try_get_idle())
end

local function release_agent(addr)
	local name = tostring(addr)

	-- move from running to idle
	idle_agents[name], running_agents[name] = addr, nil
	running_count, idle_count = running_count - 1, idle_count + 1
	assert(running_count >= 0 and idle_count >= 0)

	-- 有空闲的agent，通知wait_queue
	for k, v in pairs(idle_agents) do
		local co = table.remove(wait_queue, 1)
		if not co then
			break	-- wait_queue为空了
		end

		skynet.wakeup(co)	-- 唤醒　
	end
end

local full_begin_time = 0		-- 全忙开始时间
local warn_full_time = 10*100	-- 全忙告警时间
local function check()
	local now = skynet.now()
	if idle_count > 0 then
		full_begin_time = now
		return
	end

	local d = now - full_begin_time
	if d > warn_full_time then
		local timestamp = os.date("%Y-%m-%d %H:%M:%S", os.time())
		skynet.error(timestamp, "WARNING!", "blocking threads busy!!!", d/100)
	end
end

skynet.start(function()
	skynet.error("start blocking, max threads:", max_threads)
	create_blocking_agents(max_threads)

	skynet.dispatch("lua", function (session, address, cmd, ...)
		if cmd == "get" then
			local timeout = ...
			local addr = get_idle_service(timeout)
			return skynet.ret(skynet.pack(addr))
		end

		if cmd == "put" then
			local addr = ...
			release_agent(addr)
			return
		end

		error(string.format("Unknown blocking %s", tostring(cmd)))
	end)

	-- 全忙检测
	skynet.fork(function()
		while true do
			skynet.sleep(500)
			check()
		end
	end)
end)
