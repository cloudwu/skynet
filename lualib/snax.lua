local skynet = require "skynet"
local snax_interface = require "snax_interface"

local snax = {}
local typeclass = {}

local G = { require = function() end }

function snax.interface(name)
	if typeclass[name] then
		return typeclass[name]
	end

	local si = snax_interface(name, G)

	local ret = {
		subscribe = {},
		response = {},
		system = {},
	}

	for _,v in ipairs(si) do
		local id, group, name, f = table.unpack(v)
		ret[group][name] = id
	end

	typeclass[name] = ret
	return ret
end

local meta = { __tostring = function(v) return string.format("[%s:%x]", v.type, v.handle) end}

local skynet_send = skynet.send
local skynet_call = skynet.call

local function gen_pub(type, handle)
	return setmetatable({} , {
		__index = function( t, k )
			local id = assert(type.subscribe[k] , string.format("publish %s no exist", k))
			return function(...)
				skynet_send(handle, "lua", id, ...)
			end
		end })
end

local function gen_req(type, handle)
	return setmetatable({} , {
		__index = function( t, k )
			local id = assert(type.response[k] , string.format("request %s no exist", k))
			return function(...)
				return skynet_call(handle, "lua", id, ...)
			end
		end })
end

local function wrapper(handle, name, type)
	return setmetatable ({
		pub = gen_pub(type, handle),
		req = gen_req(type, handle),
		type = name,
		handle = handle,
		}, meta)
end

local handle_cache = setmetatable( {} , { __mode = "kv" } )

function snax.newservice(name, ...)
	local t = snax.interface(name)
	local handle = skynet.newservice("snaxd", name)
	assert(handle_cache[handle] == nil)
	if t.system.init then
		skynet.call(handle, "lua", t.system.init, ...)
	end
	local ret = wrapper(handle, name, t)
	handle_cache[handle] = ret
	return ret
end

function snax.bind(handle, type)
	local ret = handle_cache[handle]
	if ret then
		assert(ret.type == type)
		return ret
	end
	local t = snax.interface(type)
	ret = wrapper(handle, type, t)
	handle_cache[handle] = ret
	return ret
end

function snax.kill(obj, ...)
	local t = snax.interface(obj.type)
	skynet_call(obj.handle, "lua", t.system.exit, ...)
end

local function test_result(ok, ...)
	if ok then
		return ...
	else
		error(...)
	end
end

function snax.hotfix(obj, source, ...)
	local t = snax.interface(obj.type)
	return test_result(skynet_call(obj.handle, "lua", t.system.hotfix, source, ...))
end

return snax
