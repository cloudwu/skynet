local skynet = require "skynet"
local dump = require "skynet.datasheet.dump"
local core = require "skynet.datasheet.core"
local service = require "skynet.service"

local builder = {}

local cache = {}
local dataset = {}
local address

local unique_id = 0
local function unique_string(str)
	unique_id = unique_id + 1
	return str .. tostring(unique_id)
end

local function monitor(pointer)
	skynet.fork(function()
		skynet.call(address, "lua", "collect", pointer)
		for k,v in pairs(cache) do
			if v == pointer then
				cache[k] = nil
				return
			end
		end
	end)
end

local function dumpsheet(v)
	if type(v) == "string" then
		return v
	else
		return dump.dump(v)
	end
end

function builder.new(name, v)
	assert(dataset[name] == nil)
	local datastring = unique_string(dumpsheet(v))
	local pointer = core.stringpointer(datastring)
	skynet.call(address, "lua", "update", name, pointer)
	cache[datastring] = pointer
	dataset[name] = datastring
	monitor(pointer)
end

function builder.update(name, v)
	local lastversion = assert(dataset[name])
	local newversion = dumpsheet(v)
	local diff = unique_string(dump.diff(lastversion, newversion))
	local pointer = core.stringpointer(diff)
	skynet.call(address, "lua", "update", name, pointer)
	cache[diff] = pointer
	local lp = assert(cache[lastversion])
	skynet.send(address, "lua", "release", lp)
	dataset[name] = diff
	monitor(pointer)
end

function builder.compile(v)
	return dump.dump(v)
end

local function datasheet_service()

local skynet = require "skynet"

local datasheet = {}
local handles = {}	-- handle:{ ref:count , name:name , collect:resp }
local dataset = {}	-- name:{ handle:handle, monitor:{monitors queue} }

local function releasehandle(source, handle)
	local h = handles[handle]
	h.ref = h.ref - 1
	if h.ref == 0 and h.collect then
		h.collect(true)
		h.collect = nil
		handles[handle] = nil
	end
	local t=dataset[h.name]
	t.monitor[source]=nil
end

-- from builder, create or update handle
function datasheet.update(source, name, handle)
	local t = dataset[name]
	if not t then
		-- new datasheet
		t = { handle = handle, monitor = {} }
		dataset[name] = t
		handles[handle] = { ref = 1, name = name }
	else
		-- report update to customers
		handles[handle] = { ref = handles[t.handle].ref, name = name }
		t.handle = handle

		for k,v in pairs(t.monitor) do
			v(true, handle)
			t.monitor[k] = nil
		end
	end
	skynet.ret()
end

-- from customers
function datasheet.query(source, name)
	local t = assert(dataset[name], "create data first")
	local handle = t.handle
	local h = handles[handle]
	h.ref = h.ref + 1
	skynet.ret(skynet.pack(handle))
end

-- from customers, monitor handle change
function datasheet.monitor(source, handle)
	local h = assert(handles[handle], "Invalid data handle")
	local t = dataset[h.name]
	if t.handle ~= handle then	-- already changes
		skynet.ret(skynet.pack(t.handle))
	else
		assert(not t.monitor[source])
		t.monitor[source]=skynet.response()
	end
end

-- from customers, release handle , ref count - 1
function datasheet.release(source, handle)
	-- send message, don't ret
	releasehandle(source, handle)
end

-- from builder, monitor handle release
function datasheet.collect(source, handle)
	local h = assert(handles[handle], "Invalid data handle")
	if h.ref == 0 then
		handles[handle] = nil
		skynet.ret()
	else
		assert(h.collect == nil, "Only one collect allows")
		h.collect = skynet.response()
	end
end

skynet.dispatch("lua", function(_,source,cmd,...)
	datasheet[cmd](source,...)
end)

skynet.info_func(function()
	local info = {}
	local tmp = {}
	for k,v in pairs(handles) do
		tmp[k] = v
	end
	for k,v in pairs(dataset) do
		local h = handles[v.handle]
		tmp[v.handle] = nil
		info[k] = {
			handle = v.handle,
			monitors = h.ref,
		}
	end
	for k,v in pairs(tmp) do
		info[k] = v.ref
	end

	return info
end)

end

skynet.init(function()
	address=service.new("datasheet", datasheet_service)
end)

return builder
