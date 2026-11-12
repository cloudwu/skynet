local skynet = require "skynet"
local profile = require "skynet.profile"
local tinsert = table.insert


----------------------------------------------------
-- simple call tree profiler

local profiler = {}
local calltree
local currnode

local function newnode(info)
	return {
		func = info.func,
		name = info.name,
		what = info.what,
		start = profile.elapsed(),
		time = 0,
		count = 1,
		parent = nil,
		children = {},
	}
end

local function hook(ev)
	if ev == "call" then
		local info = debug.getinfo(2, "nSf")
		local node
		if currnode then
			for _, nd in ipairs(currnode.children) do
				if nd.func == info.func then
					node = nd
					break
				end
			end
			if node then
				node.count = node.count + 1
				node.start = profile.elapsed()
			else
				node = newnode(info)
				tinsert(currnode.children, node)
			end
			node.parent = currnode
		else
			node = newnode(info)
			tinsert(calltree.children, node)
		end
		currnode = node
	elseif ev == "return" then
		if not currnode then
			return
		end
		local elapsed = profile.elapsed() - currnode.start
		currnode.time = currnode.time + elapsed
		currnode = currnode.parent
	end
end

function profiler.start()
	currnode = nil
	calltree = {
		children = {},
	}
	profile.start()
	debug.sethook(hook, "cr")
end

function profiler.stop()
	debug.sethook()
	profile.stop()
end

function profiler.print(level)
	print(string.format("%-30s%15s%15s%15s%30s%15s", "name", "time", "time self", "count", "func", "what"))
	print(string.rep("-", 130))

	local function calc_timeself(node)
		local ti = node.time
		for _, child in ipairs(node.children) do
			ti = ti - child.time
		end
		return ti
	end

	local function print_nodes(nodes, indent)
		for _, node in ipairs(nodes) do
			local name = string.format("%s%s", string.rep(" ", indent*2), node.name)
			local timeself = calc_timeself(node)
			print(string.format("%-30s%15s%15s%15s%30s%15s", name, node.time, timeself, node.count, node.func, node.what))
			if indent < level then
				print_nodes(node.children, indent + 1)
			end
		end
	end

	level = level or 3
	print_nodes(calltree.children, 0)
end

----------------------------------------------------
-- test

local function insert(a, v)
	for i = #a, 1, -1 do
		a[i+1] = a[i]
	end
	a[1] = tostring(v)
end

local function concat(a)
	local s = ""
	for i = 1, #a do
		s = s ..a[i]
	end
end

local function test1()
	local a = {}
	for i = 1, 10000 do
		insert(a, i)
	end
	skynet.sleep(100)
	concat(a)
end

local function test2()
	local function fibonacci(n)
		if n == 1 or n == 2 then
			return 1
		else
			return fibonacci(n - 1) + fibonacci(n - 2)
		end
	end
	fibonacci(26)
end

skynet.start(function()
	skynet.timeout(0, function()
		local t = skynet.time()
		profiler.start()
		test1()
		test2()
		profiler.stop()
		print("skynet.time", skynet.time() - t)
		profiler.print(6)
	end)
end)
