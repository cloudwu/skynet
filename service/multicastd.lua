local skynet = require "skynet"
local mc = require "multicast.c"
local datacenter = require "datacenter"

local harbor_id = skynet.harbor(skynet.self())

local command = {}
local channel = {}
local channel_n = {}
local channel_remote = {}
local channel_id = harbor_id

local function get_address(t, id)
	local v = assert(datacenter.get("multicast", id))
	t[id] = v
	return v
end

local node_address = setmetatable({}, { __index = get_address })

function command.NEW()
	channel[channel_id] = {}
	channel_n[channel_id] = 0
	local ret = channel_id
	channel_id = channel_id + 256
	return ret
end

local function remote_publish(node, channel, source, ...)
	skynet.redirect(node_address[node], source, "multicast", channel, ...)
end

local function publish(c , source, pack, size)
	local group = assert(channel[c])
	mc.bind(pack, channel_n[c])
	local msg = skynet.tostring(pack, size)
	for k in pairs(group) do
		skynet.redirect(k, source, "multicast", c , msg)
	end
	local remote = channel_remote[c]
	if remote then
		local _, msg, sz = mc.unpack(pack, size)
		local msg = skynet.tostring(msg,sz)
		for node in pairs(remote) do
			remote_publish(node, c, source, msg)
		end
	end
end

skynet.register_protocol {
	name = "multicast",
	id = skynet.PTYPE_MULTICAST,
	unpack = function(msg, sz)
		return mc.packremote(msg, sz)
	end,
	dispatch = publish,
}

function command.PUB(source, c, pack, size)
	assert(skynet.harbor(source) == harbor_id)
	local node = c % 256
	if node ~= harbor_id then
		-- remote publish
		remote_publish(node, c, source, mc.remote(pack))
	else
		publish(c, source, pack,size)
	end
end

function command.SUBR(source, c)
	local node = skynet.harbor(source)
	assert(node ~= harbor_id)
	local group = channel_remote[c]
	if group == nil then
		group = {}
		channel_remote[c] = group
	end
	group[node] = true
end

function command.SUB(source, c)
	local node = c % 256
	if node ~= harbor_id then
		-- remote group
		if channel[c] == nil then
			channel[c] = {}
			channel_n[c] = 0
			skynet.call(node_address[node], "lua", "SUBR", c)
		end
	end
	local group = assert(channel[c])
	if not group[source] then
		channel_n[c] = channel_n[c] + 1
		group[source] = true
	end
end

function command.USUBR(source, c)
	local node = skynet.harbor(source)
	assert(node ~= harbor_id)
	local group = assert(channel_remote[c])
	group[node] = nil
end

function command.USUB(source, c)
	local group = assert(channel[c])
	if group[source] then
		group[source] = nil
		channel_n[c] = channel_n[c] - 1
		if channel_n[c] == 0 then
			local node = c % 256
			if node ~= harbor_id then
				-- remote group
				channel[c] = nil
				channel_n[c] = nil
				skynet.call(node_address[node], "lua", "USUBR", c)
			end
		end
	end
end

skynet.start(function()
	skynet.dispatch("lua", function(_,source, cmd, ...)
		local f = assert(command[cmd])
		skynet.ret(skynet.pack(f(source, ...)))
	end)
	local self = skynet.self()
	local id = skynet.harbor(self)
	assert(datacenter.set("multicast", id, self) == nil)
end)

