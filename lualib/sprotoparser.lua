local lpeg = require "lpeg"
local table = require "table"

local packbytes
local packvalue

if _VERSION == "Lua 5.3" then
	function packbytes(str)
		return string.pack("<s4",str)
	end

	function packvalue(id)
		id = (id + 1) * 2
		return string.pack("<I2",id)
	end
else
	function packbytes(str)
		local size = #str
		local a = size % 256
		size = math.floor(size / 256)
		local b = size % 256
		size = math.floor(size / 256)
		local c = size % 256
		size = math.floor(size / 256)
		local d = size
		return string.char(a)..string.char(b)..string.char(c)..string.char(d) .. str
	end

	function packvalue(id)
		id = (id + 1) * 2
		assert(id >=0 and id < 65536)
		local a = id % 256
		local b = math.floor(id / 256)
		return string.char(a) .. string.char(b)
	end
end

local P = lpeg.P
local S = lpeg.S
local R = lpeg.R
local C = lpeg.C
local Ct = lpeg.Ct
local Cg = lpeg.Cg
local Cc = lpeg.Cc
local V = lpeg.V

local function count_lines(_,pos, parser_state)
	if parser_state.pos < pos then
		parser_state.line = parser_state.line + 1
		parser_state.pos = pos
	end
	return pos
end

local exception = lpeg.Cmt( lpeg.Carg(1) , function ( _ , pos, parser_state)
	error(string.format("syntax error at [%s] line (%d)", parser_state.file or "", parser_state.line))
	return pos
end)

local eof = P(-1)
local newline = lpeg.Cmt((P"\n" + "\r\n") * lpeg.Carg(1) ,count_lines)
local line_comment = "#" * (1 - newline) ^0 * (newline + eof)
local blank = S" \t" + newline + line_comment
local blank0 = blank ^ 0
local blanks = blank ^ 1
local alpha = R"az" + R"AZ" + "_"
local alnum = alpha + R"09"
local word = alpha * alnum ^ 0
local name = C(word)
local typename = C(word * ("." * word) ^ 0)
local tag = R"09" ^ 1 / tonumber
local mainkey = "(" * blank0 * name * blank0 * ")"

local function multipat(pat)
	return Ct(blank0 * (pat * blanks) ^ 0 * pat^0 * blank0)
end

local function namedpat(name, pat)
	return Ct(Cg(Cc(name), "type") * Cg(pat))
end

local typedef = P {
	"ALL",
	FIELD = namedpat("field", (name * blanks * tag * blank0 * ":" * blank0 * (C"*")^0 * typename * mainkey^0)),
	STRUCT = P"{" * multipat(V"FIELD" + V"TYPE") * P"}",
	TYPE = namedpat("type", P"." * name * blank0 * V"STRUCT" ),
	SUBPROTO = Ct((C"request" + C"response") * blanks * (name + V"STRUCT")),
	PROTOCOL = namedpat("protocol", name * blanks * tag * blank0 * P"{" * multipat(V"SUBPROTO") * P"}"),
	ALL = multipat(V"TYPE" + V"PROTOCOL"),
}

local proto = blank0 * typedef * blank0

local convert = {}

function convert.protocol(all, obj)
	local result = { tag = obj[2] }
	for _, p in ipairs(obj[3]) do
		assert(result[p[1]] == nil)
		local typename = p[2]
		if type(typename) == "table" then
			local struct = typename
			typename = obj[1] .. "." .. p[1]
			all.type[typename] = convert.type(all, { typename, struct })
		end
		result[p[1]] = typename
	end
	return result
end

function convert.type(all, obj)
	local result = {}
	local typename = obj[1]
	local tags = {}
	local names = {}
	for _, f in ipairs(obj[2]) do
		if f.type == "field" then
			local name = f[1]
			if names[name] then
				error(string.format("redefine %s in type %s", name, typename))
			end
			names[name] = true
			local tag = f[2]
			if tags[tag] then
				error(string.format("redefine tag %d in type %s", tag, typename))
			end
			tags[tag] = true
			local field = { name = name, tag = tag }
			table.insert(result, field)
			local fieldtype = f[3]
			if fieldtype == "*" then
				field.array = true
				fieldtype = f[4]
			end
			local mainkey = f[5]
			if mainkey then
				assert(field.array)
				field.key = mainkey
			end
			field.typename = fieldtype
		else
			assert(f.type == "type")	-- nest type
			local nesttypename = typename .. "." .. f[1]
			f[1] = nesttypename
			assert(all.type[nesttypename] == nil, "redefined " .. nesttypename)
			all.type[nesttypename] = convert.type(all, f)
		end
	end
	table.sort(result, function(a,b) return a.tag < b.tag end)
	return result
end

local function adjust(r)
	local result = { type = {} , protocol = {} }

	for _, obj in ipairs(r) do
		local set = result[obj.type]
		local name = obj[1]
		assert(set[name] == nil , "redefined " .. name)
		set[name] = convert[obj.type](result,obj)
	end

	return result
end

local buildin_types = {
	integer = 0,
	boolean = 1,
	string = 2,
}

local function checktype(types, ptype, t)
	if buildin_types[t] then
		return t
	end
	local fullname = ptype .. "." .. t
	if types[fullname] then
		return fullname
	else
		ptype = ptype:match "(.+)%..+$"
		if ptype then
			return checktype(types, ptype, t)
		elseif types[t] then
			return t
		end
	end
end

local function flattypename(r)
	for typename, t in pairs(r.type) do
		for _, f in pairs(t) do
			local ftype = f.typename
			local fullname = checktype(r.type, typename, ftype)
			if fullname == nil then
				error(string.format("Undefined type %s in type %s", ftype, typename))
			end
			f.typename = fullname
		end
	end

	return r
end

local function parser(text,filename)
	local state = { file = filename, pos = 0, line = 1 }
	local r = lpeg.match(proto * -1 + exception , text , 1, state )
	return flattypename(adjust(r))
end

--[[
-- The protocol of sproto
.type {
	.field {
		name 0 : string
		buildin	1 :	integer
		type 2 : integer
		tag	3 :	integer
		array 4	: boolean
		key 5 : integer # If key exists, array must be true, and it's a map.
	}
	name 0 : string
	fields 1 : *field
}

.protocol {
	name 0 : string
	tag	1 :	integer
	request	2 :	integer	# index
	response 3 : integer # index
}

.group {
	type 0 : *type
	protocol 1 : *protocol
}
]]

local function packfield(f)
	local strtbl = {}
	if f.array then
		if f.key then
			table.insert(strtbl, "\6\0")  -- 6 fields
		else
			table.insert(strtbl, "\5\0")  -- 5 fields
		end
	else
		table.insert(strtbl, "\4\0")	-- 4 fields
	end
	table.insert(strtbl, "\0\0")	-- name	(tag = 0, ref an object)
	if f.buildin then
		table.insert(strtbl, packvalue(f.buildin))	-- buildin (tag = 1)
		table.insert(strtbl, "\1\0")	-- skip (tag = 2)
		table.insert(strtbl, packvalue(f.tag))		-- tag (tag = 3)
	else
		table.insert(strtbl, "\1\0")	-- skip (tag = 1)
		table.insert(strtbl, packvalue(f.type))		-- type (tag = 2)
		table.insert(strtbl, packvalue(f.tag))		-- tag (tag = 3)
	end
	if f.array then
		table.insert(strtbl, packvalue(1))	-- array = true (tag = 4)
	end
	if f.key then
		table.insert(strtbl, packvalue(f.key)) -- key tag (tag = 5)
	end
	table.insert(strtbl, packbytes(f.name)) -- external object (name)
	return packbytes(table.concat(strtbl))
end

local function packtype(name, t, alltypes)
	local fields = {}
	local tmp = {}
	for _, f in ipairs(t) do
		tmp.array = f.array
		tmp.name = f.name
		tmp.tag = f.tag

		tmp.buildin = buildin_types[f.typename]
		local subtype
		if not tmp.buildin then
			subtype = assert(alltypes[f.typename])
			tmp.type = subtype.id
		else
			tmp.type = nil
		end
		if f.key then
			tmp.key = subtype.fields[f.key]
			if not tmp.key then
				error("Invalid map index :" .. f.key)
			end
		else
			tmp.key = nil
		end

		table.insert(fields, packfield(tmp))
	end
	local data
	if #fields == 0 then
		data = {
			"\1\0",	-- 1 fields
			"\0\0",	-- name	(id = 0, ref = 0)
			packbytes(name),
		}
	else
		data = {
			"\2\0",	-- 2 fields
			"\0\0",	-- name	(tag = 0, ref = 0)
			"\0\0", -- field[]	(tag = 1, ref = 1)
			packbytes(name),
			packbytes(table.concat(fields)),
		}
	end

	return packbytes(table.concat(data))
end

local function packproto(name, p, alltypes)
--	if p.request == nil then
--		error(string.format("Protocol %s need request", name))
--	end
	if p.request then
		local request = alltypes[p.request]
		if request == nil then
			error(string.format("Protocol %s request type %s not found", name, p.request))
		end
		request = request.id
	end
	local tmp = {
		"\4\0",	-- 4 fields
		"\0\0",	-- name (id=0, ref=0)
		packvalue(p.tag),	-- tag (tag=1)
	}
	if p.request == nil and p.response == nil then
		tmp[1] = "\2\0"
	else
		if p.request then
			table.insert(tmp, packvalue(alltypes[p.request].id)) -- request typename (tag=2)
		else
			table.insert(tmp, "\1\0")
		end
		if p.response then
			table.insert(tmp, packvalue(alltypes[p.response].id)) -- request typename (tag=3)
		else
			tmp[1] = "\3\0"
		end
	end

	table.insert(tmp, packbytes(name))

	return packbytes(table.concat(tmp))
end

local function packgroup(t,p)
	if next(t) == nil then
		assert(next(p) == nil)
		return "\0\0"
	end
	local tt, tp
	local alltypes = {}
	for name in pairs(t) do
		table.insert(alltypes, name)
	end
	table.sort(alltypes)	-- make result stable
	for idx, name in ipairs(alltypes) do
		local fields = {}
		for _, type_fields in ipairs(t[name]) do
			if buildin_types[type_fields.typename] then
				fields[type_fields.name] = type_fields.tag
			end
		end
		alltypes[name] = { id = idx - 1, fields = fields }
	end
	tt = {}
	for _,name in ipairs(alltypes) do
		table.insert(tt, packtype(name, t[name], alltypes))
	end
	tt = packbytes(table.concat(tt))
	if next(p) then
		local tmp = {}
		for name, tbl in pairs(p) do
			table.insert(tmp, tbl)
			tbl.name = name
		end
		table.sort(tmp, function(a,b) return a.tag < b.tag end)

		tp = {}
		for _, tbl in ipairs(tmp) do
			table.insert(tp, packproto(tbl.name, tbl, alltypes))
		end
		tp = packbytes(table.concat(tp))
	end
	local result
	if tp == nil then
		result = {
			"\1\0",	-- 1 field
			"\0\0",	-- type[] (id = 0, ref = 0)
			tt,
		}
	else
		result = {
			"\2\0",	-- 2fields
			"\0\0",	-- type array	(id = 0, ref = 0)
			"\0\0",	-- protocol array	(id = 1, ref =1)

			tt,
			tp,
		}
	end

	return table.concat(result)
end

local function encodeall(r)
	return packgroup(r.type, r.protocol)
end

local sparser = {}

function sparser.dump(str)
	local tmp = ""
	for i=1,#str do
		tmp = tmp .. string.format("%02X ", string.byte(str,i))
		if i % 8 == 0 then
			if i % 16 == 0 then
				print(tmp)
				tmp = ""
			else
				tmp = tmp .. "- "
			end
		end
	end
	print(tmp)
end

function sparser.parse(text, name)
	local r = parser(text, name or "=text")
	local data = encodeall(r)
	return data
end

return sparser
