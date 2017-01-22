local reload = {}
local sandbox = {}

local table = table
local debug = debug

do -- sandbox begin

local function findloader(name)
	if reload.postfix then
		name = name .. reload.postfix
	end
	local msg = {}
	for _, loader in ipairs(package.searchers) do
		local f , extra = loader(name)
		local t = type(f)
		if t == "function" then
			return f, extra
		elseif t == "string" then
			table.insert(msg, f)
		end
	end
	error(string.format("module '%s' not found:%s", name, table.concat(msg)))
end

local global_mt = {
	__newindex = error,
	__pairs = error,
	__metatable = "SANDBOX",
}
local _LOADED_DUMMY = {}
local _LOADED = {}
local weak = { __mode = "kv" }
local dummy_cache
local dummy_module_cache

local module_dummy_mt = {
	__metatable = "MODULE",
	__newindex = error,
	__pairs = error,
	__tostring = function(self) return dummy_module_cache[self] end,
}

local function make_dummy_module(name)
	local name = "[" .. name .. "]"
	if dummy_module_cache[name] then
		return dummy_module_cache[name]
	else
		local obj = {}
		dummy_module_cache[name] = obj
		dummy_module_cache[obj] = name
		return setmetatable(obj, module_dummy_mt)
	end
end

function module_dummy_mt:__index(k)
	assert(type(k) == "string", "module field is not string")
	local parent_key = dummy_module_cache[self]
	local key = parent_key .. "." .. k
	if dummy_module_cache[key] then
		return dummy_module_cache[key]
	else
		local obj = {}
		dummy_module_cache[key] = obj
		dummy_module_cache[obj] = key
		return setmetatable(obj, module_dummy_mt)
	end
end

local function make_sandbox()
	return setmetatable({}, global_mt)
end

function sandbox.require(name)
	assert(type(name) == "string")
	if _LOADED_DUMMY[name] then
		return _LOADED_DUMMY[name]
	end
	local loader, arg = findloader(name)
	local env, uv = debug.getupvalue(loader, 1)
	if env == "_ENV" then
		debug.setupvalue(loader, 1, make_sandbox())
	end
	local ret = loader(name, arg) or true
	_LOADED[name] = { module = ret }
	if env == "_ENV" then
		debug.setupvalue(loader, 1, nil)
		_LOADED[name].loader = loader
	end
	_LOADED_DUMMY[name] = make_dummy_module(name)
	return _LOADED_DUMMY[name]
end

local global_dummy_mt = {
	__metatable = "GLOBAL",
	__tostring = function(self)	return dummy_cache[self] end,
	__newindex = error,
	__pairs = error,
}

local function make_dummy(k)
	if dummy_cache[k] then
		return dummy_cache[k]
	else
		local obj = {}
		dummy_cache[obj] = k
		dummy_cache[k] = obj
		return setmetatable(obj, global_dummy_mt)
	end
end

function global_dummy_mt:__index(k)
	local parent_key = dummy_cache[self]
	assert(type(k) == "string", "Global name must be a string")
	local key = parent_key .. "." .. k
	return make_dummy(key)
end

local _inext = ipairs {}

-- the base lib function never return objects out of sandbox
local safe_function = {
	require = sandbox.require,	-- sandbox require
	pairs = pairs,	-- allow pairs during require
	next = next,
	ipairs = ipairs,
	_inext = _inext,
	print = print,	-- for debug
}

function global_mt:__index(k)
	assert(type(k) == "string", "Global name must be a string")
	if safe_function[k] then
		return safe_function[k]
	else
		return make_dummy(k)
	end
end

local function get_G(obj)
	local k = dummy_cache[obj]
	local G = _G
	for w in string.gmatch(k, "[_%a]%w*") do
		if G == nil then
			error("Invalid global", k)
		end
		G=G[w]
	end
	return G
end

local function get_M(obj)
	local k = dummy_module_cache[obj]
	local M = debug.getregistry()._LOADED
	local from, to, name = string.find(k, "^%[([_%w]+)%]")
	if from == nil then
		error ("Invalid module " .. k)
	end
	local mod = assert(M[name])
	for w in string.gmatch(k:sub(to+1), "[_%a]%w*") do
		if mod == nil then
			error("Invalid module key", k)
		end
		mod=mod[w]
	end
	return mod
end

function sandbox.value(obj)
	local meta = getmetatable(obj)
	if meta == "GLOBAL" then
		return get_G(obj)
	elseif meta == "MODULE" then
		return get_M(obj)
	else
		error("Invalid object", obj)
	end
end

function sandbox.init(list)
	dummy_cache = setmetatable({}, weak)
	dummy_module_cache = setmetatable({}, weak)

	for k,v in pairs(_LOADED_DUMMY) do
		_LOADED_DUMMY[k] = nil
	end
	for k,v in pairs(_LOADED) do
		_LOADED[k] = nil
	end
	if list then
		for _,name in ipairs(list) do
			_LOADED_DUMMY[name] = make_dummy_module(name)
		end
	end
end

function sandbox.isdummy(v)
	if safe_function[v] then
		return true
	end
	return getmetatable(v) ~= nil
end

function sandbox.module(name)
	return _LOADED[name]
end

function sandbox.clear()
	dummy_cache = nil
	dummy_module_cache = nil
	for k, v in pairs(_LOADED) do
		_LOADED[k] = nil
	end
end

end	-- sandbox end

function reload.list()
	local list = {}
	for k in pairs(debug.getregistry()._LOADED) do
		table.insert(list, k)
	end
	return list
end

local accept_key_type = {
	number = true,
	string = true,
	boolean = true,
}

local function enum_object(value)
	local print = reload.print
	local all = {}
	local path = {}
	local objs = {}
	local function iterate(value)
		if sandbox.isdummy(value) then
			if print then print("ENUM", value, table.concat(path, ".")) end
			table.insert(all, { value, table.unpack(path) })
			return
		end
		local t = type(value)
		if t == "function" or t == "table" then
			if print then print("ENUM", value, table.concat(path, ".")) end
			table.insert(all, { value, table.unpack(path) })
			if objs[value] then
				-- already unfold
				return
			end
			objs[value] = true
		else
			return
		end
		local depth = #path + 1
		if t == "function" then
			local i = 1
			while true do
				local name, v = debug.getupvalue(value, i)
				if name == nil then
					break
				else
					if not name:find("^[_%w]") then
						error("Invalid upvalue : " .. table.concat(path, "."))
					end
					local vt = type(v)
					if vt == "function" or vt == "table" then
						path[depth] = name
						path[depth + 1] = i
						iterate(v)
						path[depth] = nil
						path[depth + 1] = nil
					end
				end
				i = i + 1
			end
		else	-- table
			for k,v in pairs(value) do
				if not accept_key_type[type(k)] then
					error("Invalid key : " .. k .. " " .. table.concat(path, "."))
				end
				path[depth] = k
				iterate(v)
				path[depth] = nil
			end
		end
	end
	iterate(value)
	return all
end

local function find_object(mod, name, id , ...)
	if mod == nil or name == nil then
		return mod
	end
	local t = type(mod)
	if t == "table" then
		return find_object(mod[name] , id , ...)
	else
		assert(t == "function")
		local i = 1
		while true do
			local n, value = debug.getupvalue(mod, i)
			if n == nil then
				return
			end
			if n == name then
				return find_object(value, ...)
			end
			i = i + 1
		end
	end
end

local function match_objects(objects, old_module, map, globals)
	local print = reload.print
	for _, item in ipairs(objects) do
		local obj = item[1]
		if sandbox.isdummy(obj) then
			table.insert(globals, item)
		else
			local ok, old_one = pcall(find_object,old_module, table.unpack(item, 2))
			if not ok then
				local current = { table.unpack(item, 2) }
				error ( "type mismatch : " .. table.concat(current, ",") )
			end
			if old_one == nil then
				map[obj] = map[obj] or false
			elseif type(old_one) ~= type(obj) then
				local current = { table.unpack(item, 2) }
				error ( "Not a table : " .. table.concat(current, ",") )
			end
			if map[obj] and map[obj] ~= old_one then
				local current = { table.unpack(item, 2) }
				error ( "Ambiguity table : " .. table.concat(current, ",") )
			end
			map[obj] = old_one
			if print then print("MATCH", old_one, table.unpack(item,2)) end
		end
	end
end

local function find_upvalue(func, name)
	if not func then
		return
	end
	local i = 1
	while true do
		local n,v = debug.getupvalue(func, i)
		if n == nil then
			return
		end
		if n == name then
			return i
		end
		i = i + 1
	end
end

local function match_upvalues(map, upvalues)
	for new_one , old_one in pairs(map) do
		if type(new_one) == "function" then
			local i = 1
			while true do
				local name, value = debug.getupvalue(new_one, i)
				if name ==  nil then
					break
				end
				local old_index = find_upvalue(old_one, name)
				local id = debug.upvalueid(new_one, i)
				if not upvalues[id] and old_index then
					upvalues[id] = {
						func = old_one,
						index = old_index,
						oldid = debug.upvalueid(old_one, old_index),
					}
				elseif old_index then
					local oldid = debug.upvalueid(old_one, old_index)
					if oldid ~= upvalues[id].oldid then
						error (string.format("Ambiguity upvalue : %s .%s",tostring(new_one),name))
					end
				end
				i = i + 1
			end
		end
	end
end

local function reload_list(list)
	local _LOADED = debug.getregistry()._LOADED
	local all = {}
	for _, mod in ipairs(list) do
		sandbox.require(mod)
		local m = sandbox.module(mod)
		local objs = enum_object(m.module)
		local old_module = _LOADED[mod]
		local result = {
			globals = {},
			map = {},
			upvalues = {},
			old_module = old_module,
			module = m ,
			objects = objs
		}
		all[mod] = result
		match_objects(objs, old_module, result.map, result.globals) -- find match table/func between old module and new one
		match_upvalues(result.map, result.upvalues) -- find match func's upvalues
	end
	return all
end

local function set_object(v, mod, name, tmore, fmore, ...)
	if mod == nil then
		return false
	end
	if type(mod) == "table" then
		if not tmore then	-- no more
			mod[name] = v
			return true
		end
		return set_object(v, mod[name], tmore, fmore, ...)
	else
		local i = 1
		while true do
			local n, value = debug.getupvalue(mod, i)
			if n == nil then
				return false
			end
			if n == name then
				if not fmore then
					debug.setupvalue(mod, i, v)
					return true
				end
				return set_object(v, value, fmore, ...)	-- skip tmore (id)
			end
			i = i + 1
		end
	end
end

local function patch_funcs(upvalues, map)
	local print = reload.print
	for value in pairs(map) do
		if type(value) == "function" then
			local i = 1
			while true do
				local name,v = debug.getupvalue(value, i)
				if name == nil then
					break
				end
				local id = debug.upvalueid(value, i)
				local uv = upvalues[id]
				if uv then
					if print then print("JOIN", value, name) end
					debug.upvaluejoin(value, i, uv.func, uv.index)
				end
				i = i + 1
			end
		end
	end
end

local function merge_objects(all)
	local REG = debug.getregistry()
	local _LOADED = REG._LOADED
	local print = reload.print
	for mod_name, data in pairs(all) do
		if data.old_module then
			local map = data.map
			patch_funcs(data.upvalues, map)
			for new_one, old_one in pairs(map) do
				if type(new_one) == "table" and old_one then
					-- merge new_one into old_one
					if print then print("COPY", old_one) end
					for k,v in pairs(new_one) do
						if type(v) ~= "table" or	-- copy values not a table
							getmetatable(v) ~= nil or -- copy dummy
							old_one[k] == nil then	-- copy new object
							old_one[k] = v
						end
					end
				end
			end
			for _, item in ipairs(data.objects) do
				local v = item[1]
				if not sandbox.isdummy(v) then
					if not map[v] then
						-- insert new object
						local ok = set_object(v, data.old_module, table.unpack(item,2))
						if print then print("MOVE", mod_name, table.concat(item,".",2),ok) end
					end
				end
			end
		else
			_LOADED[mod_name] = data.module.module
		end
	end
end

local function slove_globals(all)
	local _LOADED = debug.getregistry()._LOADED
	local print = reload.print
	local i = 0
	for mod_name, data in pairs(all) do
		for gk, item in pairs(data.globals) do
			-- slove one global
			local v = item[1]
			local path = tostring(v)
			local value
			local unsloved
			local invalid
			if getmetatable(v) == "GLOBAL" then
				local G = _G
				for w in string.gmatch(path, "[_%a]%w*") do
					if G == nil then
						invalid = true
						break
					end
					G=G[w]
				end
				value = G
			else
				-- "MODULE"
				local from, to, name = string.find(path, "^%[([_%w]+)%]")
				if from == nil then
					invalid = true
					break
				end
					local mod = _LOADED[name]
					if mod == nil then
						invalid = true
						break
					end
					for w in string.gmatch(path:sub(to+1), "[_%a]%w*") do
						if mod == nil then
							invalid = true
							break
						end
						mod=mod[w]
					end
					local mt = getmetatable(mod)
					if mt == "MODULE" then
					else
						unsloved = true
						value = mod
					end
			end
			if invalid then
				if print then print("GLOBAL INVALID", path) end
				data.globals[gk] = nil
			elseif not unsloved then
				i = i + 1
				if print then print("GLOBAL", path, value) end
				set_object(value, _LOADED[mod_name], table.unpack(item,2))
				data.globals[gk] = nil
			end
		end
	end
	return i
end

local function update_funcs(map)
	local root = debug.getregistry()
	local co = coroutine.running()
	local exclude = { [map] = true , [co] = true }
	local getmetatable = debug.getmetatable
	local getinfo = debug.getinfo
	local getlocal = debug.getlocal
	local setlocal = debug.setlocal
	local getupvalue = debug.getupvalue
	local setupvalue = debug.setupvalue
	local getuservalue = debug.getuservalue
	local setuservalue = debug.setuservalue
	local type = type
	local next = next
	local rawset = rawset

	exclude[exclude] = true


	local update_funcs_

	local function update_funcs_frame(co,level)
		local info = getinfo(co, level+1, "f")
		if info == nil then
			return
		end
		local f = info.func
		info = nil
		update_funcs_(f)
		local i = 1
		while true do
			local name, v = getlocal(co, level+1, i)
			if name == nil then
				if i > 0 then
					i = -1
				else
					break
				end
			end
			local nv = map[v]
			if nv then
				setlocal(co, level+1, i, nv)
				update_funcs_(nv)
			else
				update_funcs_(v)
			end
			if i > 0 then
				i = i + 1
			else
				i = i - 1
			end
		end
		return update_funcs_frame(co, level+1)
	end

	function update_funcs_(root)	-- local function
		if exclude[root] then
			return
		end
		local t = type(root)
		if t == "table" then
			exclude[root] = true
			local mt = getmetatable(root)
			if mt then update_funcs_(mt) end
			local tmp
			for k,v in next, root do
				local nv = map[v]
				if nv then
					rawset(root,k,nv)
					update_funcs_(nv)
				else
					update_funcs_(v)
				end
				local nk = map[k]
				if nk then
					if tmp == nil then
						tmp = {}
					end
					tmp[k] = nk
				else
					update_funcs_(k)
				end
			end
			if tmp then
				for k,v in next, tmp do
					root[k], root[v] = nil, root[k]
					update_funcs_(v)
				end
				tmp = nil
			end
		elseif t == "userdata" then
			exclude[root] = true
			local mt = getmetatable(root)
			if mt then update_funcs_(mt) end
			local uv = getuservalue(root)
			if uv then
				local tmp = map[uv]
				if tmp then
					setuservalue(root, tmp)
					update_funcs_(tmp)
				else
					update_funcs_(uv)
				end
			end
		elseif t == "thread" then
			exclude[root] = true
			update_funcs_frame(root,2)
		elseif t == "function" then
			exclude[root] = true
			local i = 1
			while true do
				local name, v = getupvalue(root, i)
				if name == nil then
					break
				else
					local nv = map[v]
					if nv then
						setupvalue(root, i, nv)
						update_funcs_(nv)
					else
						update_funcs_(v)
					end
				end
				i=i+1
			end
		end
	end

	-- nil, number, boolean, string, thread, function, lightuserdata may have metatable
	for _,v in pairs { nil, 0, true, "", co, update_funcs, debug.upvalueid(update_funcs,1) } do
		local mt = getmetatable(v)
		if mt then update_funcs_(mt) end
	end

	update_funcs_frame(co, 2)
	update_funcs_(root)
end

function reload.reload(list)
	local print = reload.print
	local REG = debug.getregistry()
	local _LOADED = REG._LOADED
	local need_reload = {}
	for _,mod in ipairs(list) do
		need_reload[mod] = true
	end
	local tmp = {}
	for k in pairs(_LOADED) do
		if not need_reload[k] then
			table.insert(tmp, k)
		end
	end
	sandbox.init(tmp)	-- init dummy modoule existed

	local ok, result = xpcall(reload_list, debug.traceback, list)
	if not ok then
		sandbox.clear()
		if print then print("ERROR", result) end
		return ok, result
	end

	merge_objects(result)

	for _, data in pairs(result) do
		if data.module.loader then
			debug.setupvalue(data.module.loader, 1, _ENV)
		end
	end

	repeat
		local n = slove_globals(result)
	until n == 0

	local func_map = {}
	for _, data in pairs(result) do
		for k,v in pairs(data.map) do
			if type(k) == "function" then
				func_map[v] = k
			end
		end
	end
	result = nil
	sandbox.clear()

	update_funcs(func_map)

	return true
end

return reload
