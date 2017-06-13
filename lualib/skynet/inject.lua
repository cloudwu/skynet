local function getupvaluetable(u, func, unique)
	local i = 1
	while true do
		local name, value = debug.getupvalue(func, i)
		if name == nil then
			return
		end
		local t = type(value)
		if t == "table" then
			u[name] = value
		elseif t == "function" then
			if not unique[value] then
				unique[value] = true
				getupvaluetable(u, value, unique)
			end
		end
		i=i+1
	end
end

return function(skynet, source, filename , ...)
	if filename then
		filename = "@" .. filename
	else
		filename = "=(load)"
	end
	local output = {}

	local function print(...)
		local value = { ... }
		for k,v in ipairs(value) do
			value[k] = tostring(v)
		end
		table.insert(output, table.concat(value, "\t"))
	end
	local u = {}
	local unique = {}
	local funcs = { ... }
	for k, func in ipairs(funcs) do
		getupvaluetable(u, func, unique)
	end
	local p = {}
	local proto = u.proto
	if proto then
		for k,v in pairs(proto) do
			local name, dispatch = v.name, v.dispatch
			if name and dispatch and not p[name] then
				local pp = {}
				p[name] = pp
				getupvaluetable(pp, dispatch, unique)
			end
		end
	end
	local env = setmetatable( { print = print , _U = u, _P = p}, { __index = _ENV })
	local func, err = load(source, filename, "bt", env)
	if not func then
		return false, { err }
	end
	local ok, err = skynet.pcall(func)
	if not ok then
		table.insert(output, err)
		return false, output
	end

	return true, output
end
