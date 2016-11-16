local skynet = require "skynet"
local sharedata = require "sharedata.corelib"
local table = table
local cache = require "skynet.codecache"
cache.mode "OFF"	-- turn off codecache, because CMD.new may load data file

local NORET = {}
local pool = {}
local pool_count = {}
local objmap = {}
local collect_tick = 600

local function newobj(name, tbl)
	assert(pool[name] == nil)
	local cobj = sharedata.host.new(tbl)
	sharedata.host.incref(cobj)
	local v = { value = tbl , obj = cobj, watch = {} }
	objmap[cobj] = v
	pool[name] = v
	pool_count[name] = { n = 0, threshold = 16 }
end

local function collect10sec()
	if collect_tick > 10 then
		collect_tick = 10
	end
end

local function collectobj()
	while true do
		skynet.sleep(100)	-- sleep 1s
		if collect_tick <= 0 then
			collect_tick = 600	-- reset tick count to 600 sec
			collectgarbage()
			for obj, v in pairs(objmap) do
				if v == true then
					if sharedata.host.getref(obj) <= 0  then
						objmap[obj] = nil
						sharedata.host.delete(obj)
					end
				end
			end
		else
			collect_tick = collect_tick - 1
		end
	end
end

local CMD = {}

local env_mt = { __index = _ENV }

function CMD.new(name, t, ...)
	local dt = type(t)
	local value
	if dt == "table" then
		value = t
	elseif dt == "string" then
		value = setmetatable({}, env_mt)
		local f
		if t:sub(1,1) == "@" then
			f = assert(loadfile(t:sub(2),"bt",value))
		else
			f = assert(load(t, "=" .. name, "bt", value))
		end
		local _, ret = assert(skynet.pcall(f, ...))
		setmetatable(value, nil)
		if type(ret) == "table" then
			value = ret
		end
	elseif dt == "nil" then
		value = {}
	else
		error ("Unknown data type " .. dt)
	end
	newobj(name, value)
end

function CMD.delete(name)
	local v = assert(pool[name])
	pool[name] = nil
	pool_count[name] = nil
	assert(objmap[v.obj])
	objmap[v.obj] = true
	sharedata.host.decref(v.obj)
	for _,response in pairs(v.watch) do
		response(true)
	end
end

function CMD.query(name)
	local v = assert(pool[name])
	local obj = v.obj
	sharedata.host.incref(obj)
	return v.obj
end

function CMD.confirm(cobj)
	if objmap[cobj] then
		sharedata.host.decref(cobj)
	end
	return NORET
end

function CMD.update(name, t, ...)
	local v = pool[name]
	local watch, oldcobj
	if v then
		watch = v.watch
		oldcobj = v.obj
		objmap[oldcobj] = true
		sharedata.host.decref(oldcobj)
		pool[name] = nil
		pool_count[name] = nil
	end
	CMD.new(name, t, ...)
	local newobj = pool[name].obj
	if watch then
		sharedata.host.markdirty(oldcobj)
		for _,response in pairs(watch) do
			response(true, newobj)
		end
	end
	collect10sec()	-- collect in 10 sec
end

local function check_watch(queue)
	local n = 0
	for k,response in pairs(queue) do
		if not response "TEST" then
			queue[k] = nil
			n = n + 1
		end
	end
	return n
end

function CMD.monitor(name, obj)
	local v = assert(pool[name])
	if obj ~= v.obj then
		return v.obj
	end

	local n = pool_count[name].n + 1
	if n > pool_count[name].threshold then
		n = n - check_watch(v.watch)
		pool_count[name].threshold = n * 2
	end
	pool_count[name].n = n

	table.insert(v.watch, skynet.response())

	return NORET
end

skynet.start(function()
	skynet.fork(collectobj)
	skynet.dispatch("lua", function (session, source ,cmd, ...)
		local f = assert(CMD[cmd])
		local r = f(...)
		if r ~= NORET then
			skynet.ret(skynet.pack(r))
		end
	end)
end)

