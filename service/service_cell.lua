local skynet = require "skynet"

local service_name = (...)
local init = {}

function init.init(code, ...)
	skynet.dispatch("lua", function() error("No dispatch function")	end)

	local init_func = {}
	function skynet.init(f, name)
		assert(type(f) == "function")
		if init_func == nil then
			f()
		else
			table.insert(init_func, f)
			if name then
				assert(type(name) == "string")
				assert(init_func[name] == nil)
				init_func[name] = f
			end
		end
	end

	local function init_all()
		local funcs = init_func
		init_func = nil
		if funcs then
			for _,f in ipairs(funcs) do
				f()
			end
		end
	end

	local function ret(f, ...)
		f()
		return ...
	end

	function skynet.start(start_func)
		init_all()
		init_func = {}
		return ret(init_all, start_func())
	end
	local mainfunc = assert(load(code, service_name))
	mainfunc(...)
	skynet.ret()
end

skynet.start(function()
	skynet.dispatch("lua", function(_,_,cmd,...)
		init[cmd](...)
	end)
end)
