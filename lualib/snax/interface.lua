local skynet = require "skynet"

local function dft_loader(path, name, G)
	local errlist = {}

	for pat in string.gmatch(path,"[^;]+") do
		local filename = string.gsub(pat, "?", name)
		local f , err = loadfile(filename, "bt", G)
		if f then
			return f, pat
		else
			table.insert(errlist, err)
		end
	end

	error(table.concat(errlist, "\n"))
end

return function (name , G, loader)
	loader = loader or dft_loader

	local function func_id(id, group)
		local tmp = {}
		local function count( _, name, func)
			if type(name) ~= "string" then
				error (string.format("%s method only support string", group))
			end
			if type(func) ~= "function" then
				error (string.format("%s.%s must be function"), group, name)
			end
			if tmp[name] then
				error (string.format("%s.%s duplicate definition", group, name))
			end
			tmp[name] = true
			table.insert(id, { #id + 1, group, name, func} )
		end
		return setmetatable({}, { __newindex = count })
	end

	do
		assert(getmetatable(G) == nil)
		assert(G.init == nil)
		assert(G.exit == nil)
		assert(G.accept == nil)
		assert(G.response == nil)
	end

	local temp_global = {}
	local env = setmetatable({} , { __index = temp_global })
	local func = {}

	local system = { "init", "exit", "hotfix", "profile"}

	do
		for k, v in ipairs(system) do
			system[v] = k
			func[k] = { k , "system", v }
		end
	end

	env.accept = func_id(func, "accept")
	env.response = func_id(func, "response")

	local function init_system(t, name, f)
		local index = system[name]
		if index then
			if type(f) ~= "function" then
				error (string.format("%s must be a function", name))
			end
			func[index][4] = f
		else
			temp_global[name] = f
		end
	end

	local path = assert(skynet.getenv "snax" , "please set snax in config file")
	local mainfunc, pattern = loader(path, name, G)

	setmetatable(G,	{ __index = env , __newindex = init_system })
	local ok, err = xpcall(mainfunc, debug.traceback)
	setmetatable(G, nil)
	assert(ok,err)

	for k,v in pairs(temp_global) do
		G[k] = v
	end

	return func, pattern
end
