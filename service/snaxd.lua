local skynet = require "skynet"
local snax_interface = require "snax_interface"

local func = snax_interface(tostring(...), _ENV)

skynet.start(function()
	local init = false
	skynet.dispatch("lua", function ( _, _, id, ...)
		local method = func[id]
		if method[2] == "system" then
			local command = method[3]
			if command == "hotfix" then
				local hotfix = require "snax_hotfix"
				local ok, msg = hotfix(func, ...)
				if ok then
					local hf = func[id][4]
					skynet.ret(skynet.pack(pcall(hf, select(2,...))))
				else
					skynet.ret(skynet.pack(ok, msg))
				end
			elseif command == "init" then
				assert(not init, "Already init")
				local initfunc = method[4] or function() end
				skynet.ret(skynet.pack(initfunc(...)))
				init = true
			else
				assert(init, "Never init")
				assert(command == "exit")
				local exitfunc = method[4] or function() end
				skynet.ret(skynet.pack(exitfunc(...)))
				init = false
				skynet.exit()
			end
		else
			assert(init, "Init first")
			if method[2] == "subscribe" then
				-- no return
				method[4](...)
			else
				skynet.ret(skynet.pack(method[4](...)))
			end
		end
	end)
end)
