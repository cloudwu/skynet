local skynet = require "skynet"
local mc = require "multicast.core"

local multicastd
local multicast = {}
local dispatch = setmetatable({} , {__mode = "kv" })

local chan = {}
local chan_meta = {
	__index = chan,
	__gc = function(self)
		self:unsubscribe()
	end,
	__tostring = function (self)
		return string.format("[Multicast:%x]",self.channel)
	end,
}

local function default_conf(conf)
	conf = conf or {}
	conf.pack = conf.pack or skynet.pack
	conf.unpack = conf.unpack or skynet.unpack

	return conf
end

function multicast.new(conf)
	assert(multicastd, "Init first")
	local self = {}
	conf = conf or self
	self.channel = conf.channel
	if self.channel == nil then
		self.channel = skynet.call(multicastd, "lua", "NEW")
	end
	self.__pack = conf.pack or skynet.pack
	self.__unpack = conf.unpack or skynet.unpack
	self.__dispatch = conf.dispatch

	return setmetatable(self, chan_meta)
end

function chan:delete()
	local c = assert(self.channel)
	skynet.send(multicastd, "lua", "DEL", c)
	self.channel = nil
	self.__subscribe = nil
end

function chan:publish(...)
	local c = assert(self.channel)
	skynet.call(multicastd, "lua", "PUB", c, mc.pack(self.__pack(...)))
end

function chan:subscribe()
	local c = assert(self.channel)
	if self.__subscribe then
		-- already subscribe
		return
	end
	skynet.call(multicastd, "lua", "SUB", c)
	self.__subscribe = true
	dispatch[c] = self
end

function chan:unsubscribe()
	if not self.__subscribe then
		-- already unsubscribe
		return
	end
	local c = assert(self.channel)
	skynet.send(multicastd, "lua", "USUB", c)
	self.__subscribe = nil
end

local function dispatch_subscribe(channel, source, pack, msg, sz)
	local self = dispatch[channel]
	if not self then
		mc.close(pack)
		error ("Unknown channel " .. channel)
	end

	if self.__subscribe then
		local ok, err = pcall(self.__dispatch, self, source, self.__unpack(msg, sz))
		mc.close(pack)
		assert(ok, err)
	else
		-- maybe unsubscribe first, but the message is send out. drop the message unneed
		mc.close(pack)
	end
end

local function init()
	multicastd = skynet.uniqueservice "multicastd"
	skynet.register_protocol {
		name = "multicast",
		id = skynet.PTYPE_MULTICAST,
		unpack = mc.unpack,
		dispatch = dispatch_subscribe,
	}
end

skynet.init(init, "multicast")

return multicast