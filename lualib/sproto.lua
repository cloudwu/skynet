local core = require "sproto.core"
local assert = assert

local sproto = {}
local host = {}

local weak_mt = { __mode = "kv" }
local sproto_mt = { __index = sproto }
local sproto_nogc = { __index = sproto }
local host_mt = { __index = host }

function sproto_mt:__gc()
	core.deleteproto(self.__cobj)
end

function sproto.new(bin)
	local cobj = assert(core.newproto(bin))
	local self = {
		__cobj = cobj,
		__tcache = setmetatable( {} , weak_mt ),
		__pcache = setmetatable( {} , weak_mt ),
	}
	return setmetatable(self, sproto_mt)
end

function sproto.sharenew(cobj)
	local self = {
		__cobj = cobj,
		__tcache = setmetatable( {} , weak_mt ),
		__pcache = setmetatable( {} , weak_mt ),
	}
	return setmetatable(self, sproto_nogc)
end

function sproto.parse(ptext)
	local parser = require "sprotoparser"
	local pbin = parser.parse(ptext)
	return sproto.new(pbin)
end

function sproto:host( packagename )
	packagename = packagename or  "package"
	local obj = {
		__proto = self,
		__package = assert(core.querytype(self.__cobj, packagename), "type package not found"),
		__session = {},
	}
	return setmetatable(obj, host_mt)
end

local function querytype(self, typename)
	local v = self.__tcache[typename]
	if not v then
		v = assert(core.querytype(self.__cobj, typename), "type not found")
		self.__tcache[typename] = v
	end

	return v
end

function sproto:exist_type(typename)
	local v = self.__tcache[typename]
	if not v then
		return core.querytype(self.__cobj, typename) ~= nil
	else
		return true
	end
end

function sproto:encode(typename, tbl)
	local st = querytype(self, typename)
	return core.encode(st, tbl)
end

function sproto:decode(typename, ...)
	local st = querytype(self, typename)
	return core.decode(st, ...)
end

function sproto:pencode(typename, tbl)
	local st = querytype(self, typename)
	return core.pack(core.encode(st, tbl))
end

function sproto:pdecode(typename, ...)
	local st = querytype(self, typename)
	return core.decode(st, core.unpack(...))
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

function sproto:exist_proto(pname)
	local v = self.__pcache[pname]
	if not v then
		return core.protocol(self.__cobj, pname) ~= nil
	else
		return true
	end
end

function sproto:request_encode(protoname, tbl)
	local p = queryproto(self, protoname)
	local request = p.request
	if request then
		return core.encode(request,tbl) , p.tag
	else
		return "" , p.tag
	end
end

function sproto:response_encode(protoname, tbl)
	local p = queryproto(self, protoname)
	local response = p.response
	if response then
		return core.encode(response,tbl)
	else
		return ""
	end
end

function sproto:request_decode(protoname, ...)
	local p = queryproto(self, protoname)
	local request = p.request
	if request then
		return core.decode(request,...) , p.name
	else
		return nil, p.name
	end
end

function sproto:response_decode(protoname, ...)
	local p = queryproto(self, protoname)
	local response = p.response
	if response then
		return core.decode(response,...)
	end
end

sproto.pack = core.pack
sproto.unpack = core.unpack

function sproto:default(typename, type)
	if type == nil then
		return core.default(querytype(self, typename))
	else
		local p = queryproto(self, typename)
		if type == "REQUEST" then
			if p.request then
				return core.default(p.request)
			end
		elseif type == "RESPONSE" then
			if p.response then
				return core.default(p.response)
			end
		else
			error "Invalid type"
		end
	end
end

local header_tmp = {}

local function gen_response(self, response, session)
	return function(args, ud, pack)
		header_tmp.type = nil
		header_tmp.session = session
		header_tmp.ud = ud
		local header = core.encode(self.__package, header_tmp)
		pack = pack or core.pack
		if response then
			local content = core.encode(response, args)
			return pack(header .. content)
		else
			return pack(header)
		end
	end
end

function host:dispatch(...)
	local bin = core.unpack(...)
	header_tmp.type = nil
	header_tmp.session = nil
	header_tmp.ud = nil
	local header, size = core.decode(self.__package, bin, header_tmp)
	local content = bin:sub(size + 1)
	if header.type then
		-- request
		local proto = queryproto(self.__proto, header.type)
		local result
		if proto.request then
			result = core.decode(proto.request, content)
		end
		if header_tmp.session then
			return "REQUEST", proto.name, result, gen_response(self, proto.response, header_tmp.session), header.ud
		else
			return "REQUEST", proto.name, result, nil, header.ud
		end
	else
		-- response
		local session = assert(header_tmp.session, "session not found")
		local response = assert(self.__session[session], "Unknown session")
		self.__session[session] = nil
		if response == true then
			return "RESPONSE", session, nil, header.ud
		else
			local result = core.decode(response, content)
			return "RESPONSE", session, result, header.ud
		end
	end
end

function host:attach(sp)
	return function(name, args, session, ud, pack)
		local proto = queryproto(sp, name)
		header_tmp.type = proto.tag
		header_tmp.session = session
		header_tmp.ud = ud
		local header = core.encode(self.__package, header_tmp)

		if session then
			self.__session[session] = proto.response or true
		end

		pack = pack or core.pack
		if proto.request then
			local content = core.encode(proto.request, args)
			return pack(header ..  content)
		else
			return pack(header)
		end
	end
end

return sproto
