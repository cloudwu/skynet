local skynet = require "skynet"
local group = require "mcgroup"
local id, g = ...

skynet.dispatch(function (msg,sz)
	print("===>",id, skynet.tostring(msg,sz))
end
)

skynet.start(function()
	if g then
		group.enter(tonumber(g))
	end
end)
