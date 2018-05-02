local stm = require "skynet.stm"
local sprotoloader = require "sprotoloader"
local sproto = require "sproto"
local setmetatable = setmetatable

local sharemap = {}

function sharemap.register(protofile)
	-- use global slot 0 for type define
	sprotoloader.register(protofile, 0)
end

local sprotoobj
local function loadsp()
	if sprotoobj == nil then
		sprotoobj = sprotoloader.load(0)
	end
	return sprotoobj
end

function sharemap:commit()
	self.__obj(sprotoobj:encode(self.__typename, self.__data))
end

function sharemap:copy()
	return stm.copy(self.__obj)
end

function sharemap.writer(typename, obj)
	local sp = loadsp()
	obj = obj or {}
	local stmobj = stm.new(sp:encode(typename,obj))
	local ret = {
		__typename = typename,
		__obj = stmobj,
		__data = obj,
		commit = sharemap.commit,
		copy = sharemap.copy,
	}
	return setmetatable(ret, { __index = obj, __newindex = obj })
end

local function decode(msg, sz, self)
	local data = self.__data
	for k in pairs(data) do
		data[k] = nil
	end
	return sprotoobj:decode(self.__typename, msg, sz, data)
end

function sharemap:update()
	return self.__obj(decode, self)
end

function sharemap.reader(typename, stmcpy)
	local sp = loadsp()
	local stmobj = stm.newcopy(stmcpy)
	local _, data = stmobj(function(msg, sz)
		return sp:decode(typename, msg, sz)
	end)

	local obj = {
		__typename = typename,
		__obj = stmobj,
		__data = data,
		update = sharemap.update,
	}
	return setmetatable(obj, { __index = data, __newindex = error })
end

return sharemap
