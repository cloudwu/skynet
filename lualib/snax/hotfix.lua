local si = require "snax.interface"
local io = io

local hotfix = {}

local function envid(f)
	local i = 1
	while true do
		local name, value = debug.getupvalue(f, i)
		if name == nil then
			return
		end
		if name == "_ENV" then
			return debug.upvalueid(f, i)
		end
		i = i + 1
	end
end

local function collect_uv(f , uv, env)
	local i = 1
	while true do
		local name, value = debug.getupvalue(f, i)
		if name == nil then
			break
		end
		local id = debug.upvalueid(f, i)

		if uv[name] then
			assert(uv[name].id == id, string.format("ambiguity local value %s", name))
		else
			uv[name] = { func = f, index = i, id = id }

			if type(value) == "function" then
				if envid(value) == env then
					collect_uv(value, uv, env)
				end
			end
		end

		i = i + 1
	end
end

local function collect_all_uv(funcs)
	local global = {}
	for _, v in pairs(funcs) do
		if v[4] then
			collect_uv(v[4], global, envid(v[4]))
		end
	end

	return global
end

local function loader(source)
	return function (filename, ...)
		return load(source, "=patch", ...)
	end
end

local function find_func(funcs, group , name)
	for _, desc in pairs(funcs) do
		local _, g, n = table.unpack(desc)
		if group == g and name == n then
			return desc
		end
	end
end

local dummy_env = {}

local function patch_func(funcs, global, group, name, f)
	local desc = assert(find_func(funcs, group, name) , string.format("Patch mismatch %s.%s", group, name))
	local i = 1
	while true do
		local name, value = debug.getupvalue(f, i)
		if name == nil then
			break
		elseif value == nil or value == dummy_env then
			local old_uv = global[name]
			if old_uv then
				debug.upvaluejoin(f, i, old_uv.func, old_uv.index)
			end
		end
		i = i + 1
	end
	desc[4] = f
end

local function inject(funcs, source, ...)
	local patch = si("patch", dummy_env, loader(source))
	local global = collect_all_uv(funcs)

	for _, v in pairs(patch) do
		local _, group, name, f = table.unpack(v)
		if f then
			patch_func(funcs, global, group, name, f)
		end
	end

	local hf = find_func(patch, "system", "hotfix")
	if hf and hf[4] then
		return hf[4](...)
	end
end

return function (funcs, source, ...)
	return pcall(inject, funcs, source, ...)
end
