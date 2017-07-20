local skynet = require "skynet"
local mc = require "skynet.multicast.core"
local datacenter = require "skynet.datacenter"

local harbor_id = skynet.harbor(skynet.self())

local command = {}
local channel = {}
local channel_n = {}
local channel_remote = {}
local channel_id = harbor_id
local NORET = {}

local function get_address(t, id)
	local v = assert(datacenter.get("multicast", id))
	t[id] = v
	return v
end

local node_address = setmetatable({}, { __index = get_address })

-- new LOCAL channel , The low 8bit is the same with harbor_id
function command.NEW()
	while channel[channel_id] do
		channel_id = mc.nextid(channel_id)
	end
	channel[channel_id] = {}
	channel_n[channel_id] = 0
	local ret = channel_id
	channel_id = mc.nextid(channel_id)
	return ret
end

-- MUST call by the owner node of channel, delete a remote channel
function command.DELR(source, c)
	channel[c] = nil
	channel_n[c] = nil
	return NORET
end

-- delete a channel, if the channel is remote, forward the command to the owner node
-- otherwise, delete the channel, and call all the remote node, DELR
function command.DEL(source, c)
	local node = c % 256
	if node ~= harbor_id then
		skynet.send(node_address[node], "lua", "DEL", c)
		return NORET
	end
	local remote = channel_remote[c]
	channel[c] = nil
	channel_n[c] = nil
	channel_remote[c] = nil
	if remote then
		for node in pairs(remote) do
			skynet.send(node_address[node], "lua", "DELR", c)
		end
	end
	return NORET
end

-- forward multicast message to a node (channel id use the session field)
local function remote_publish(node, channel, source, ...)
	skynet.redirect(node_address[node], source, "multicast", channel, ...)
end

-- publish a message, for local node, use the message pointer (call mc.bind to add the reference)
-- for remote node, call remote_publish. (call mc.unpack and skynet.tostring to convert message pointer to string)
local function publish(c , source, pack, size)
	local remote = channel_remote[c]
	if remote then
		-- remote publish should unpack the pack, because we should not publish the pointer out.
		local _, msg, sz = mc.unpack(pack, size)
		local msg = skynet.tostring(msg,sz)
		for node in pairs(remote) do
			remote_publish(node, c, source, msg)
		end
	end

	local group = channel[c]
	if group == nil or next(group) == nil then
		-- dead channel, delete the pack. mc.bind returns the pointer in pack and free the pack (struct mc_package **)
		local pack = mc.bind(pack, 1)
		mc.close(pack)
		return
	end
	local msg = skynet.tostring(pack, size)	-- copy (pack,size) to a string
	mc.bind(pack, channel_n[c])	-- mc.bind will free the pack(struct mc_package **)
	for k in pairs(group) do
		-- the msg is a pointer to the real message, publish pointer in local is ok.
		skynet.redirect(k, source, "multicast", c , msg)
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

-- publish a message, if the caller is remote, forward the message to the owner node (by remote_publish)
-- If the caller is local, call publish
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

-- the node (source) subscribe a channel
-- MUST call by channel owner node (assert source is not local and channel is create by self)
-- If channel is not exist, return true
-- Else set channel_remote[channel] true
function command.SUBR(source, c)
	local node = skynet.harbor(source)
	if not channel[c] then
		-- channel none exist
		return true
	end
	assert(node ~= harbor_id and c % 256 == harbor_id)
	local group = channel_remote[c]
	if group == nil then
		group = {}
		channel_remote[c] = group
	end
	group[node] = true
end

-- the service (source) subscribe a channel
-- If the channel is remote, node subscribe it by send a SUBR to the owner .
function command.SUB(source, c)
	local node = c % 256
	if node ~= harbor_id then
		-- remote group
		if channel[c] == nil then
			if skynet.call(node_address[node], "lua", "SUBR", c) then
				return
			end
			if channel[c] == nil then
				-- double check, because skynet.call whould yield, other SUB may occur.
				channel[c] = {}
				channel_n[c] = 0
			end
		end
	end
	local group = channel[c]
	if group and not group[source] then
		channel_n[c] = channel_n[c] + 1
		group[source] = true
	end
end

-- MUST call by a node, unsubscribe a channel
function command.USUBR(source, c)
	local node = skynet.harbor(source)
	assert(node ~= harbor_id)
	local group = assert(channel_remote[c])
	group[node] = nil
	return NORET
end

-- Unsubscribe a channel, if the subscriber is empty and the channel is remote, send USUBR to the channel owner
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
				skynet.send(node_address[node], "lua", "USUBR", c)
			end
		end
	end
	return NORET
end

skynet.start(function()
	skynet.dispatch("lua", function(_,source, cmd, ...)
		local f = assert(command[cmd])
		local result = f(source, ...)
		if result ~= NORET then
			skynet.ret(skynet.pack(result))
		end
	end)
	local self = skynet.self()
	local id = skynet.harbor(self)
	assert(datacenter.set("multicast", id, self) == nil)
end)

