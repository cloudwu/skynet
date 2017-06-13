local debug = debug
local table = table

local FUNC_TEMP=[[
local $ARGS
return function(...)
$SOURCE
end,
function()
return {$LOCALS}
end
]]

local temp = {}
local function wrap_locals(co, source, level, ext_funcs)
	if co == coroutine.running() then
		level = level + 3
	end
	local f = debug.getinfo(co, level,"f").func
	if f == nil then
		return false, "Invalid level"
	end

	local uv = {}
	local locals = {}
	local uv_id = {}
	local local_id = {}

	if ext_funcs then
		for k,v in pairs(ext_funcs) do
			table.insert(uv, k)
		end
	end
	local i = 1
	while true do
		local name, value = debug.getlocal(co, level, i)
		if name == nil then
			break
		end
		if name:byte() ~= 40 then	-- '('
			table.insert(uv, name)
			table.insert(locals, ("[%d]=%s,"):format(i,name))
			local_id[name] = value
		end
		i = i + 1
	end
	local i = 1
	while true do
		local name = debug.getupvalue(f, i)
		if name == nil then
			break
		end
		uv_id[name] = i
		table.insert(uv, name)
		i = i + 1
	end
	temp.ARGS = table.concat(uv, ",")
	temp.SOURCE = source
	temp.LOCALS = table.concat(locals)
	local full_source = FUNC_TEMP:gsub("%$(%w+)",temp)
	local loader, err = load(full_source, "=(debug)")
	if loader == nil then
		return false, err
	end
	local func, update = loader()
	-- join func's upvalues
	local i = 1
	while true do
		local name = debug.getupvalue(func, i)
		if name == nil then
			break
		end
		if ext_funcs then
			local v = ext_funcs[name]
			if v then
				debug.setupvalue(func, i, v)
			end
		end

		local local_value = local_id[name]
		if local_value then
			debug.setupvalue(func, i, local_value)
		end
		local upvalue_id = uv_id[name]
		if upvalue_id then
			debug.upvaluejoin(func, i, f, upvalue_id)
		end
		i=i+1
	end
	local vararg, v = debug.getlocal(co, level, -1)
	if vararg then
		local vargs = { v }
		local i = 2
		while true do
			local vararg,v = debug.getlocal(co, level, -i)
			if vararg then
				vargs[i] = v
			else
				break
			end
			i=i+1
		end
		return func, update, table.unpack(vargs)
	else
		return func, update
	end
end

local function exec(co, level, func, update, ...)
	if not func then
		return false, update
	end
	if co == coroutine.running() then
		level = level + 2
	end
	local rets = table.pack(pcall(func, ...))
	if rets[1] then
		local needupdate = update()
		for k,v in pairs(needupdate) do
			debug.setlocal(co, level,k,v)
		end
		return table.unpack(rets, 1, rets.n)
	else
		return false, rets[2]
	end
end

return function (source, co, level, ext_funcs)
	co = co or coroutine.running()
	level = level or 0
	return exec(co, level, wrap_locals(co, source, level, ext_funcs))
end

