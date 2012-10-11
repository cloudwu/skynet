local skynet = require "skynet"

local local_group = {}
for i=1,10000 do
	local_group[i] = true
end

local command = {}

function command.NEW()
	local k = next(local_group)
	if k then
		local_group[k] = nil
		skynet.ret(skynet.pack(k))
	else
		skynet.ret(skynet.pack(nil))
	end
end

function command.DELETE(id)
	assert(handle_group[id] == nil)
	skynet.clear_group(id)
	local_group[id] = true
end

skynet.start(function()
	skynet.dispatch("lua", function(_, _, cmd, id)
		local f = command[cmd]
		assert(f, cmd, id)
		f(id)
	end)
end)
