local core = require "sproto.core"

local sproto = {}
local rpc = {}

local weak_mt = { __mode = "kv" }
local sproto_mt = { __index = sproto }
local rpc_mt = { __index = rpc }

function sproto_mt:__gc()
	core.deleteproto(self.__cobj)
end

function sproto.new(pbin)
	local cobj = assert(core.newproto(pbin))
	local self = {
		__cobj = cobj,
		__tcache = setmetatable( {} , weak_mt ),
		__pcache = setmetatable( {} , weak_mt ),
	}
	return setmetatable(self, sproto_mt)
end

function sproto.parse(ptext)
	local parser = require "sprotoparser"
	local pbin = parser.parse(ptext)
	return sproto.new(pbin)
end

function sproto:rpc( packagename )
	packagename = packagename or  "package"
	local obj = {
		__proto = self,
		__package = core.querytype(self.__cobj, packagename),
		__session = {},
	}
	return setmetatable(obj, rpc_mt)
end

local function querytype(self, typename)
	local v = self.__tcache[typename]
	if not v then
		v = core.querytype(self.__cobj, typename)
		self.__tcache[typename] = v
	end

	return v
end

function sproto:encode(typename, tbl)
	local st = querytype(self, typename)
	return core.encode(st, tbl)
end

function sproto:decode(typename, bin)
	local st = querytype(self, typename)
	return core.decode(st, bin)
end

function sproto:pencode(typename, tbl)
	local st = querytype(self, typename)
	return core.pack(core.encode(st, tbl))
end

function sproto:pdecode(typename, bin)
	local st = querytype(self, typename)
	return core.decode(st, core.unpack(bin))
end

local function queryproto(self, pname)
	local v = self.__pcache[pname]
	if not v then
		local tag, req, resp = core.protocol(self.__cobj, pname)
		assert(tag, pname .. " not found")
		if tonumber(pname) then
			pname, tag = tag, pname
		end
		v = {
			request = req,
			response =resp,
			name = pname,
			tag = tag,
		}
		self.__pcache[pname] = v
		self.__pcache[tag]  = v
	end

	return v
end

local header_tmp = {}
function rpc:request(name, args, session)
	local proto = queryproto(self.__proto, name)
	header_tmp.type = proto.tag
	header_tmp.session = session
	local header = core.encode(self.__package, header_tmp)

	if session then
		self.__session[session] = assert(proto.response)
	end

	if args then
		local content = core.encode(proto.request, args)
		return core.pack(header ..  content)
	else
		return core.pack(header)
	end
end

local function gen_response(self, response, session)
	return function(args)
		header_tmp.type = nil
		header_tmp.session = session
		local header = core.encode(self.__package, header_tmp)
		local content = core.encode(response, args)
		return core.pack(header .. content)
	end
end

function rpc:dispatch(...)
	local bin = core.unpack(...)
	header_tmp.type = nil
	header_tmp.session = nil
	local header, size = core.decode(self.__package, bin, header_tmp)
	local content = bin:sub(size + 1)
	if header.type then
		-- request
		local proto = queryproto(self.__proto, header.type)
		local result = core.decode(proto.request, content)
		if header_tmp.session then
			return "REQUEST", proto.name, result, gen_response(self, proto.response, header_tmp.session)
		else
			return "REQUEST", proto.name, result
		end
	else
		-- response
		local session = assert(header_tmp.session, "session not found")
		local response = assert(self.__session[session], "Unknown session")
		self.__session[session] = nil
		return "RESPONSE", session, core.decode(response, content)
	end
end

return sproto
