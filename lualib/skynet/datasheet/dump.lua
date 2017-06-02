--[[ file format
document :
  int32 strtbloffset
  int32 n
  int32*n index table
  table*n
  strings

table:
  int32 array
  int32 dict
  int8*(array+dict) type (align 4)
  value*array
  kvpair*dict

kvpair:
  string k
  value v

value: (union)
  int32 integer
  float real
  int32 boolean
  int32 table index
  int32 string offset

type: (enum)
  0 nil
  1 integer
  2 real
  3 boolean
  4 table
  5 string
]]

local ctd = {}
local math = math
local table = table
local string = string

function ctd.dump(root)
	local doc = {
		table_n = 0,
		table = {},
		strings = {},
		offset = 0,
	}
	local function dump_table(t)
		local index = doc.table_n + 1
		doc.table_n = index
		doc.table[index] = false	-- place holder
		local array_n = 0
		local array = {}
		local kvs = {}
		local types = {}
		local function encode(v)
			local t = type(v)
			if t == "table" then
				local index = dump_table(v)
				return '\4', string.pack("<i4", index-1)
			elseif t == "number" then
				if math.tointeger(v) then
					return '\1', string.pack("<i4", v)
				else
					return '\2', string.pack("<f",v)
				end
			elseif t == "boolean" then
				if v then
					return '\3', "\0\0\0\1"
				else
					return '\3', "\0\0\0\0"
				end
			elseif t == "string" then
				local offset = doc.strings[v]
				if not offset then
					offset = doc.offset
					doc.offset = offset + #v + 1
					doc.strings[v] = offset
					table.insert(doc.strings, v)
				end
				return '\5', string.pack("<I4", offset)
			else
				error ("Unsupport value " .. tostring(v))
			end
		end
		for i,v in ipairs(t) do
			types[i], array[i] = encode(v)
			array_n = i
		end
		for k,v in pairs(t) do
			if type(k) == "string" then
				local _, kv = encode(k)
				local tv, ev = encode(v)
				table.insert(types, tv)
				table.insert(kvs, kv .. ev)
			else
				local ik = math.tointeger(k)
				assert(ik and ik > 0 and ik <= array_n)
			end
		end
		-- encode table
		local typeset = table.concat(types)
		local align = string.rep("\0", (4 - #typeset & 3) & 3)
		local tmp = {
			string.pack("<i4i4", array_n, #kvs),
			typeset,
			align,
			table.concat(array),
			table.concat(kvs),
		}
		doc.table[index] = table.concat(tmp)
		return index
	end
	dump_table(root)
	-- encode document
	local index = {}
	local offset = 0
	for i, v in ipairs(doc.table) do
		index[i] = string.pack("<I4", offset)
		offset = offset + #v
	end
	local tmp = {
		string.pack("<I4", 4 + 4 + 4 * doc.table_n + offset),
		string.pack("<I4", doc.table_n),
		table.concat(index),
		table.concat(doc.table),
		table.concat(doc.strings, "\0")
	}
	return table.concat(tmp)
end

function ctd.undump(v)
	local stringtbl, n = string.unpack("<I4I4",v)
	local index = { string.unpack("<" .. string.rep("I4", n), v, 9) }
	local header = 4 + 4 + 4 * n + 1
	stringtbl = stringtbl + 1
	local tblidx = {}
	local function decode(n)
		local toffset = index[n+1] + header
		local array, dict = string.unpack("<I4I4", v, toffset)
		local types = { string.unpack(string.rep("B", (array+dict)), v, toffset + 8) }
		local offset = ((array + dict + 8 + 3) & ~3) + toffset
		local result = {}
		local function value(t)
			local off = offset
			offset = offset + 4
			if t == 1 then	-- integer
				return (string.unpack("<i4", v, off))
			elseif t == 2 then -- float
				return (string.unpack("<f", v, off))
			elseif t == 3 then -- boolean
				return string.unpack("<i4", v, off) ~= 0
			elseif t == 4 then -- table
				local tindex = (string.unpack("<I4", v, off))
				return decode(tindex)
			elseif t == 5 then -- string
				local sindex = string.unpack("<I4", v, off)
				return (string.unpack("z", v, stringtbl + sindex))
			else
				error (string.format("Invalid data at %d (%d)", off, t))
			end
		end
		for i=1,array do
			table.insert(result, value(types[i]))
		end
		for i=1,dict do
			local sindex = string.unpack("<I4", v, offset)
			offset = offset + 4
			local key = string.unpack("z", v, stringtbl + sindex)
			result[key] = value(types[array + i])
		end
		tblidx[result] = n
		return result
	end
	return decode(0), tblidx
end

local function diffmap(last, current)
	local lastv, lasti = ctd.undump(last)
	local curv, curi = ctd.undump(current)
	local map = {}	-- new(current index):old(last index)
	local function comp(lastr, curr)
		local old = lasti[lastr]
		local new = curi[curr]
		map[new] = old
		for k,v in pairs(lastr) do
			if type(v) == "table" then
				local newv = curr[k]
				if type(newv) == "table" then
					comp(v, newv)
				end
			end
		end
	end
	comp(lastv, curv)
	return map
end

function ctd.diff(last, current)
	local map = diffmap(last, current)
	local stringtbl, n = string.unpack("<I4I4",current)
	local _, lastn = string.unpack("<I4I4",last)
	local existn = 0
	for k,v in pairs(map) do
		existn = existn + 1
	end
	local newn = lastn
	for i = 0, n-1 do
		if not map[i] then
			map[i] = newn
			newn = newn + 1
		end
	end
	-- remap current
	local index = { string.unpack("<" .. string.rep("I4", n), current, 9) }
	local header = 4 + 4 + 4 * n + 1
	local function remap(n)
		local toffset = index[n+1] + header
		local array, dict = string.unpack("<I4I4", current, toffset)
		local types = { string.unpack(string.rep("B", (array+dict)), current, toffset + 8) }
		local hlen = (array + dict + 8 + 3) & ~3
		local hastable = false
		for _, v in ipairs(types) do
			if v == 4 then -- table
				hastable = true
				break
			end
		end
		if not hastable then
			return string.sub(current, toffset, toffset + hlen + (array + dict * 2) * 4 - 1)
		end
		local offset = hlen + toffset
		local pat = "<" .. string.rep("I4", array + dict * 2)
		local values = { string.unpack(pat, current, offset) }
		for i = 1, array do
			if types[i] == 4 then	-- table
				values[i] = map[values[i]]
			end
		end
		for i = 1, dict do
			if types[i + array] == 4 then -- table
				values[array + i * 2] = map[values[array + i * 2]]
			end
		end
		return string.sub(current, toffset, toffset + hlen - 1) ..
			string.pack(pat, table.unpack(values))
	end
	-- rebuild
	local oldindex = { string.unpack("<" .. string.rep("I4", n), current, 9) }
	local index = {}
	for i = 1, newn do
		index[i] = 0xffffffff
	end
	for i = 0, #map do
		index[map[i]+1] = oldindex[i+1]
	end

	local tmp = {
		string.pack("<I4I4", stringtbl + (newn - n) * 4, newn), -- expand index table
		string.pack("<" .. string.rep("I4", newn), table.unpack(index)),
	}
	for i = 0, n - 1 do
		table.insert(tmp, remap(i))
	end
	table.insert(tmp, string.sub(current, stringtbl+1))

	return table.concat(tmp)
end

return ctd
