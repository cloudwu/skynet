local core = require "sharedata.core"
local type = type
local next = next
local rawget = rawget

local conf = {}

conf.host = {
	new = core.new,
	delete = core.delete,
	getref = core.getref,
	markdirty = core.markdirty,
	incref = core.incref,
	decref = core.decref,
}

local meta = {}

local isdirty = core.isdirty
local index = core.index
local needupdate = core.needupdate
local len = core.len

local function findroot(self)
	while self.__parent do
		self = self.__parent
	end
	return self
end

local function update(root, cobj, gcobj)
	root.__obj = cobj
	root.__gcobj = gcobj
	local children = root.__cache
	if children then
		for k,v in pairs(children) do
			local pointer = index(cobj, k)
			if type(pointer) == "userdata" then
				update(v, pointer, gcobj)
			else
				children[k] = nil
			end
		end
	end
end

local function genkey(self)
	local key = tostring(self.__key)
	while self.__parent do
		self = self.__parent
		key = self.__key .. "." .. key
	end
	return key
end

local function getcobj(self)
	local obj = self.__obj
	if isdirty(obj) then
		local newobj, newtbl = needupdate(self.__gcobj)
		if newobj then
			local newgcobj = newtbl.__gcobj
			local root = findroot(self)
			update(root, newobj, newgcobj)
			if obj == self.__obj then
				error ("The key [" .. genkey(self) .. "] doesn't exist after update")
			end
			obj = self.__obj
		end
	end
	return obj
end

function meta:__index(key)
	local obj = getcobj(self)
	local v = index(obj, key)
	if type(v) == "userdata" then
		local children = self.__cache
		if children == nil then
			children = {}
			self.__cache = children
		end
		local r = children[key]
		if r then
			return r
		end
		r = setmetatable({
			__obj = v,
			__gcobj = self.__gcobj,
			__parent = self,
			__key = key,
		}, meta)
		children[key] = r
		return r
	else
		return v
	end
end

function meta:__len()
	return len(getcobj(self))
end

function meta:__pairs()
	return conf.next, self, nil
end

function conf.next(obj, key)
	local cobj = getcobj(obj)
	local nextkey = core.nextkey(cobj, key)
	if nextkey then
		return nextkey, obj[nextkey]
	end
end

function conf.box(obj)
	local gcobj = core.box(obj)
	return setmetatable({
		__parent = false,
		__obj = obj,
		__gcobj = gcobj,
		__key = "",
	} , meta)
end

function conf.update(self, pointer)
	local cobj = self.__obj
	assert(isdirty(cobj), "Only dirty object can be update")
	core.update(self.__gcobj, pointer, { __gcobj = core.box(pointer) })
end

function conf.flush(obj)
	getcobj(obj)
end

return conf
