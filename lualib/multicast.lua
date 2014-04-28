local skynet = require "skynet"
local c = require "multicast.c"

local multicastd
local multicast = {}
local dispatch = {}

local function default_conf(conf)
	conf = conf or {}
	conf.pack = conf.pack or skynet.pack
	conf.unpack = conf.unpack or skynet.unpack

	return conf
end

function multicast.newchannel(conf)
	assert(multicastd, "Init first")
	local channel = skynet.call(multicastd, "lua", "NEW")
	dispatch[channel] = default_conf(conf)
	return channel
end

function multicast.publish(channel, ...)
	local conf = assert(dispatch[channel])
	skynet.call(multicastd, "lua", "PUB", channel, c.pack(conf.pack(...)))
end

function multicast.subscribe(channel, conf)
	assert(multicastd, "Init first")
	assert(conf.dispatch)
	skynet.call(multicastd, "lua", "SUB", channel)
	dispatch[channel] = default_conf(conf)
end

function multicast.unsubscribe(channel)
	assert(multicastd, "Init first")
	assert(dispatch[channel])
	dispatch[channel] = nil
	skynet.call(multicastd, "lua", "USUB", channel)
end

local function dispatch_subscribe(channel, source, pack, msg, sz)
	local conf = dispatch[channel]
	if not conf then
		c.close(pack)
		error ("Unknown channel " .. channel)
	end

	local ok, err = pcall(conf.dispatch, channel, source, conf.unpack(msg, sz))
	c.close(pack)
	assert(ok, err)
end

local function init()
	multicastd = skynet.uniqueservice "multicastd"
	skynet.register_protocol {
		name = "multicast",
		id = skynet.PTYPE_MULTICAST,
		unpack = c.unpack,
		dispatch = dispatch_subscribe,
	}
end

skynet.init(init, "multicast")

return multicast