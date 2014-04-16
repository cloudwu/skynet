local skynet = require "skynet"

return function (name , G, loader)
	loader = loader or loadfile
	local mainfunc

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

		assert(G.subscribe == nil)
		assert(G.response == nil)
	end

	local env = {}
	local func = {}

	local system = { "init", "exit", "hotfix" }

	do
		for k, v in ipairs(system) do
			system[v] = k
			func[k] = { k , "system", v }
		end
	end

	env.subscribe = func_id(func, "subscribe")
	env.response = func_id(func, "response")

	local function init_system(t, name, f)
		local index = assert(system[name] , string.format("Not support global var %s", name))
		if type(f) ~= "function" then
			error (string.format("%s must be a function", name))
		end
		func[index][4] = f
	end

	setmetatable(G,	{ __index = env , __newindex = init_system })

	do
		local path = skynet.getenv "snax"

		local errlist = {}

		for pat in string.gmatch(path,"[^;]+") do
			local filename = string.gsub(pat, "?", name)
			local f , err = loader(filename, "bt", G)
			if f then
				mainfunc = f
				break
			else
				table.insert(errlist, err)
			end
		end

		if mainfunc == nil then
			error(table.concat(errlist, "\n"))
		end
	end

	mainfunc()

	setmetatable(G, nil)

	return func
end
