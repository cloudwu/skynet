local bson = require "bson"

local sub = bson.encode_order( "hello", 1, "world", 2 )

do
	-- check decode encode_order
	local d = bson.decode(sub)
	assert(d.hello == 1 )
	assert(d.world == 2 )
end

local function tbl_next(...)
	print("--- next.a", ...)
	local k, v = next(...)
	print("--- next.b", k, v)
	return k, v
end

local function tbl_pairs(obj)
	return tbl_next, obj.__data, nil
end

local obj_a = {
	__data = {
		[1] = 2,
		[3] = 4,
		[5] = 6,
	}
}

setmetatable(
	obj_a,
	{
		__index = obj_a.__data,
		__pairs = tbl_pairs,
	}
)

local obj_b = {
	__data = {
		[7] = 8,
		[9] = 10,
		[11] = obj_a,
	}
}

setmetatable(
	obj_b,
	{
		__index = obj_b.__data,
		__pairs = tbl_pairs,
	}
)

local metaarray = setmetatable({ n = 5 }, {
	__len = function(self) return self.n end,
	__index = function(self, idx) return tostring(idx) end,
})

b = bson.encode {
	a = 1,
	b = true,
	c = bson.null,
	d = { 1,2,3,4 },
	e = bson.binary "hello",
	f = bson.regex ("*","i"),
	g = bson.regex "hello",
	h = bson.date (os.time()),
	i = bson.timestamp(os.time()),
	j = bson.objectid(),
	k = { a = false, b = true },
	l = {},
	m = bson.minkey,
	n = bson.maxkey,
	o = sub,
	p = 2^32-1,
	q = obj_b,
	r = metaarray,
}

print "\n[before replace]"
t = b:decode()

for k, v in pairs(t) do
	print(k,type(v))
end

for k,v in ipairs(t.r) do
	print(k,v)
end

b:makeindex()
b.a = 2
b.b = false
b.h = bson.date(os.time())
b.i = bson.timestamp(os.time())
b.j = bson.objectid()

print "\n[after replace]"
t = b:decode()

print("o.hello", bson.type(t.o.hello))
