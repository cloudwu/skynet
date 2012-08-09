-----------------------
-- simple proto parser
-----------------------

local lpeg = require "lpeg"
local P = lpeg.P
local S = lpeg.S
local R = lpeg.R
local C = lpeg.C
local Ct = lpeg.Ct
local Cg = lpeg.Cg
local Cc = lpeg.Cc
local V = lpeg.V

local next = next
local error = error
local tonumber = tonumber
local pairs = pairs
local ipairs = ipairs
local rawset = rawset
local tinsert = table.insert
local smatch = string.match
local sbyte = string.byte

local internal_type = {
	double = "TYPE_DOUBLE",
	float = "TYPE_FLOAT",
	uint64 = "TYPE_UINT64",
	int = "TYPE_INT32",
	int32 = "TYPE_INT32",
	int64 = "TYPE_INT64",
	fixed64 = "TYPE_FIXED64",
	fixed32 = "TYPE_FIXED32",
	bool = "TYPE_BOOL",
	string = "TYPE_STRING",
	bytes = "TYPE_BYTES",
	uint32 = "TYPE_UINT32",
	sfixed32 = "TYPE_SFIXED32",
	sfixed64 = "TYPE_SFIXED64",
	sint32 = "TYPE_SINT32",
	sint64 = "TYPE_SINT64",
}

local function count_lines(_,pos, parser_state)
	if parser_state.pos < pos then
		parser_state.line = parser_state.line + 1
		parser_state.pos = pos
	end
	return pos
end

local exception = lpeg.Cmt( lpeg.Carg(1) , function ( _ , pos, parser_state)
	error( "syntax error at [" .. (parser_state.file or "") .."] (" .. parser_state.line ..")" )
	return pos
end)

local eof = P(-1)
local newline = lpeg.Cmt((P"\n" + "\r\n") * lpeg.Carg(1) ,count_lines)
local line_comment = "//" * (1 - newline) ^0 * (newline + eof)
local blank = S" \t" + newline + line_comment
local blank0 = blank ^ 0
local blanks = blank ^ 1
local alpha = R"az" + R"AZ" + "_"
local alnum = alpha + R"09"
local str_c = (1 - S("\\\"")) + P("\\") * 1
local str = P"\"" * C(str_c^0) * "\""
local dotname = ("." * alpha * alnum ^ 0) ^ 0
local typename = C(alpha * alnum ^ 0 * dotname)
local name = C(alpha * alnum ^ 0)
local filename = C((alnum + "/" + "." + "-")^1)
local id = R"09" ^ 1 / tonumber + "max" * Cc(-1)
local bool = "true" * Cc(true) + "false" * Cc(false)
local value = str + bool + name + id
local patterns = {}

local enum_item = Cg(name * blank0 * "=" * blank0 * id * blank0 * ";" * blank0)

local function insert(tbl, k,v)
	tinsert(tbl, { name = k , number = v })
	return tbl
end

patterns.ENUM = Ct(Cg("enum","type") * blanks * Cg(typename,"name") * blank0 *
	"{" * blank0 *
		Cg(lpeg.Cf(Ct"" * enum_item^1 , insert),"value")
	* "}" * blank0)

local prefix_field = P"required" * Cc"LABEL_REQUIRED" +
	P"optional" * Cc"LABEL_OPTIONAL" +
	P"repeated" * Cc"LABEL_REPEATED"
local postfix_pair = blank0 * Cg(name * blank0 * "=" * blank0 * value * blank0)
local postfix_pair_2 = blank0 * "," * postfix_pair
local postfix_field = "[" * postfix_pair * postfix_pair_2^0 * blank0 * "]"
local options = lpeg.Cf(Ct"" * postfix_field , rawset) ^ -1

local function setoption(t, options)
	if next(options) then
		t.options = options
	end
	return t
end

local message_field = lpeg.Cf (
	Ct(	Cg(prefix_field,"label") * blanks *
		Cg(typename,"type_name") * blanks *
		Cg(name,"name") * blank0 * "=" * blank0 *
		Cg(id,"number")
		) * blank0 * options ,
		setoption) * blank0 * ";" * blank0

local extensions = Ct(
	Cg("extensions" , "type") * blanks *
	Cg(id,"start") * blanks * "to" * blanks *
	Cg(id,"end") * blank0 * ";" * blank0
	)

patterns.EXTEND = Ct(
	Cg("extend", "type") * blanks *
	Cg(typename, "name") * blank0 * "{" * blank0 *
	Cg(Ct((message_field) ^ 1),"extension") * "}" * blank0
	)

patterns.MESSAGE = P { Ct(
	Cg("message","type") * blanks *
	Cg(typename,"name") * blank0 * "{" * blank0 *
	Cg(Ct((message_field + patterns.ENUM + extensions + patterns.EXTEND + V(1)) ^ 1),"items") * "}" * blank0
	) }

patterns.OPTION = Ct(
	Cg("option" , "type") * blanks *
	Cg(name, "name") * blank0 * "=" * blank0 *
	Cg(value, "value")
	) * blank0 * ";" * blank0

patterns.IMPORT = Ct( Cg("import" , "type") * blanks * Cg(filename, "name") ) * blank0

patterns.PACKAGE = Ct( Cg("package", "type") * blanks * Cg(typename, "name") ) * blank0 * ";" * blank0

local proto_tbl = { "PROTO" }

do
	local k, v = next(patterns)
	local p = V(k)
	proto_tbl[k] = v
	for k,v in next , patterns , k do
		proto_tbl[k] = v
		p = p + V(k)
	end
	proto_tbl.PROTO = Ct(blank0 * p ^ 1)
end

local proto = P(proto_tbl)

local deal = {}

function deal:import(v)
	self.dependency = self.dependency or {}
	tinsert(self.dependency , v.name)
end

function deal:package(v)
	self.package = v.name
end

function deal:enum(v)
	self.enum_type = self.enum_type or {}
	tinsert(self.enum_type , v)
end

function deal:option(v)
	self.options = self.options or {}
	self.options[v.name] = v.value
end

function deal:extend(v)
	self.extension = self.extension or {}
	local extendee = v.name
	for _,v in ipairs(v.extension) do
		v.extendee = extendee
		v.type = internal_type[v.type_name]
		if v.type then
			v.type_name = nil
		end
		tinsert(self.extension , v)
	end
end

function deal:extensions(v)
	self.extension_range = self.extension_range or {}
	tinsert(self.extension_range, v)
end

local function _add_nested_message(self, item)
	if item.type == nil then
		item.type = internal_type[item.type_name]
		if item.type then
			item.type_name = nil
		end
		self.field = self.field or {}
		tinsert(self.field, item)
	else
		local f = deal[item.type]
		item.type = nil
		f(self , item)
	end
end

function deal:message(v)
	self.nested_type = self.nested_type or {}
	local m = { name = v.name }
	tinsert(self.nested_type , m)
	for _,v in ipairs(v.items) do
		_add_nested_message(m, v)
	end
end

local function fix(r)
	local p = {}
	for _,v in ipairs(r) do
		local f = deal[v.type]
		v.type = nil
		f(p , v)
	end

	p.message_type = p.nested_type
	p.nested_type = nil

	return p
end

--- fix message name

local NULL = {}

local function _match_name(namespace , n , all)
	if sbyte(n) == 46 then
		return n
	end

	repeat
		local name = namespace .. "." .. n
		if all[name] then
			return name
		end
		namespace = smatch(namespace,"(.*)%.[%w_]+$")
	until namespace == nil
end

local function _fix_field(namespace , field, all)
	local type_name = field.type_name
	if type_name == "" then
		field.type_name = nil
		return
	elseif type_name == nil then
		return
	end

	local full_name = assert(_match_name(namespace, field.type_name, all) , field.type_name , all)

	field.type_name = full_name
	field.type = all[full_name]

	local options = field.options
	if options then
		if options.default then
			field.default_value = tostring(options.default)
			options.default = nil
		end
		if next(options) == nil then
			field.options = nil
		end
	end
end

local function _fix_extension(namespace, ext, all)
	for _,field in ipairs(ext or NULL) do
		field.extendee = assert(_match_name(namespace, field.extendee,all),field.extendee)
		_fix_field(namespace , field , all)
	end
end

local function _fix_message(msg , all)
	for _,field in ipairs(msg.field or NULL) do
		_fix_field(assert(all[msg],msg.name) , field , all)
	end
	for _,nest in ipairs(msg.nested_type or NULL) do
		_fix_message(nest , all)
	end
	_fix_extension(all[msg] , msg.extension , all)
end

local function _fix_typename(file , all)
	for _,message in ipairs(file.message_type or NULL) do
		_fix_message(message , all)
	end
	_fix_extension(file.package , file.extension , all)
end

--- merge messages

local function _enum_fullname(prefix, enum , all)
	local fullname
	if sbyte(enum.name) == 46 then
		fullname = enum.name
	else
		fullname = prefix .. "." .. enum.name
	end
	all[fullname] = "TYPE_ENUM"
	all[enum] = fullname
end

local function _message_fullname(prefix , msg , all)
	local fullname
	if sbyte(msg.name) == 46 then
		fullname = msg.name
	else
		fullname = prefix .. "." .. msg.name
	end
	all[fullname] = "TYPE_MESSAGE"
	all[msg] = fullname
	for _,nest in ipairs(msg.nested_type or NULL) do
		_message_fullname(fullname , nest , all)
	end
	for _,enum in ipairs(msg.enum_type or NULL) do
		_enum_fullname(fullname , enum , all)
	end
end

local function _gen_fullname(file , all)
	local prefix = ""
	if file.package then
		prefix = "." .. file.package
	end
	for _,message in ipairs(file.message_type or NULL) do
		_message_fullname(prefix , message , all)
	end
	for _,enum in ipairs(file.enum_type or NULL) do
		_enum_fullname(prefix , enum , all)
	end
end

--- parser

local parser = {}

local function parser_one(text,filename)
	local state = { file = filename, pos = 0, line = 1 }
	local r = lpeg.match(proto * -1 + exception , text , 1, state )
	local t = fix(r)
	return t
end

function parser.parser(text,filename)
	local t = parser_one(text,filename)
	local all = {}
	_gen_fullname(t,all)
	_fix_typename(t , all)
	return t
end

local pb = require "protobuf"

function parser.register(fileset , path)
	local all = {}
	local files = {}
	if type(fileset) == "string" then
		fileset = { fileset }
	end
	for _, filename in ipairs(fileset) do
		local fullname
		if path then
			fullname = path .. "/" .. filename
		else
			fullname = filename
		end
		local f = assert(io.open(fullname , "r"))
		local buffer = f:read "*a"
		f:close()
		local t = parser_one(buffer,filename)
		_gen_fullname(t,all)
		t.name = filename
		tinsert(files , t)
	end
	for _,file in ipairs(files) do
		_fix_typename(file,all)
	end

	local pbencode = pb.encode("google.protobuf.FileDescriptorSet" , { file = files })

	if pbencode == nil then
		error(pb.lasterror())
	end
	pb.register(pbencode)
	return files
end

return parser