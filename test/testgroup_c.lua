local skynet = require "skynet"
local group = require "mcgroup"
--local group = require "localgroup"
local id, g = ...

skynet.start(function()
	skynet.dispatch("text",function(session,address,text)
		print("===>",id, text)
	end)
	if g then
		group.enter(tonumber(g))
	end
end)
