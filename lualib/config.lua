local function config(path , pre)
	assert(path)
	local env = pre or {}
	local f = assert(loadfile(path,"t",env))
	f()
	return env
end

return config