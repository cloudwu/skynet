local skynet = require "skynet"
local mc = require "multicast.c"

local command = {}
local channel = {}
local channel_n = {}
local channel_id = skynet.harbor(skynet.self())

function command.NEW()
	channel[channel_id] = {}
	channel_n[channel_id] = 0
	local ret = channel_id
	channel_id = channel_id + 256
	return ret
end

function command.PUB(source, c, pack, size)
	local group = assert(channel[c])
	mc.bind(pack, channel_n[c])
	local msg = skynet.tostring(pack, size)
	for k in pairs(group) do
		skynet.redirect(k, source, "multicast", c , msg)
	end
end

function command.SUB(source, c)
	local group = assert(channel[c])
	if not group[source] then
		channel_n[c] = channel_n[c] + 1
		group[source] = true
	end
end

function command.USUB(source, c)
	local group = assert(channel[c])
	if group[source] then
		group[source] = nil
		channel_n[c] = channel_n[c] - 1
	end
end

skynet.register_protocol {
	name = "multicast",
	id = skynet.PTYPE_MULTICAST,
}

skynet.start(function()
	skynet.dispatch("lua", function(_,source, cmd, ...)
		local f = assert(command[cmd])
		skynet.ret(skynet.pack(f(source, ...)))
	end)
end)
