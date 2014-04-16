local si = require "snax_interface"
local io = io

local hotfix = {}

local function loader(filename, ...)
	local f = io.open(filename, "rb")
	if not f then
		return false, string.format("Can't open %s", filename)
	end
	local source = f:read "*a"
	f:close()

	return load(source, "@"..filename, ...)
end

local function check_funcs(f1,f2)
	for k,a in pairs(f1) do
		local b = f2[k]
		assert(a[1] == b[1] and a[2] == b[2] and a[3] == b[3] ,
			string.format("%s.%s %s.%s , function mismatch", a[2], a[3] , b[2], b[3]))
	end
end

local function get_upvalues(f, global)
	local uv = {}
	local i = 1
	while true do
		local name = debug.getupvalue(f, i)
		if name then
			local uid = debug.upvalueid(f, i)
			uv[name] = i
			if global[name] and global[name][1] ~= uid then
				error(string.format("ambiguity upvalue %s", name))
			end
			global[name] = { uid , f , i }
		else
			break
		end
		i = i + 1
	end
	return uv
end

local function join_upvalues(f, new_f, uv)
	local i = 1
	while true do
		local name = debug.getupvalue(new_f, i)
		if name then
			if uv[name] then
				debug.upvaluejoin(new_f, i, f, uv[name])
			end
		else
			break
		end
		i = i + 1
	end
end

local function join_global_upvalues(f, global)
	local i = 1
	while true do
		local name = debug.getupvalue(f, i)
		if name then
			local uv = global[name]
			if uv then
				debug.upvaluejoin(f, i, uv[2], uv[3])
			end
		else
			break
		end
		i = i + 1
	end
end

local function update_upvalues(funcs, newfuncs)
	local global = {}
	for id,v in pairs(funcs) do
		local f = v[4]
		local new_f = newfuncs[id][4]
		if f and new_f then
			local uv = get_upvalues(f, global)
			v[4] = new_f
			join_upvalues(f, new_f, uv)
			newfuncs[id] = nil
		end
	end
	for id,v in pairs(newfuncs) do
		if v[2] == "system" then
			funcs[id] = v
			local f = v[4]
			join_global_upvalues(f, global)
		else
			error(string.format("Detect unkown function %s.%s", v[2], v[3]))
		end
	end
end

local function inject(funcs, new)
	check_funcs(funcs, new)
	update_upvalues(funcs, new)
end

return function (funcs, modname)
	local new_funcs = si(modname, _ENV, loader)
	return pcall(inject, funcs, new_funcs)
end
