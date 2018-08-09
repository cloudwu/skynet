-- Copyright (C) 2012 Yichun Zhang (agentzh)
-- Copyright (C) 2014 Chang Feng
-- This file is modified version from https://github.com/openresty/lua-resty-mysql
-- The license is under the BSD license.
-- Modified by Cloud Wu (remove bit32 for lua 5.3)

local socketchannel = require "skynet.socketchannel"
local mysqlaux = require "skynet.mysqlaux.c"
local crypt = require "skynet.crypt"


local sub = string.sub
local strgsub = string.gsub
local strformat = string.format
local strbyte = string.byte
local strchar = string.char
local strrep = string.rep
local strunpack = string.unpack
local strpack = string.pack
local sha1= crypt.sha1
local setmetatable = setmetatable
local error = error
local tonumber = tonumber
local    new_tab = function (narr, nrec) return {} end


local _M = { _VERSION = '0.13' }
-- constants

local STATE_CONNECTED = 1
local STATE_COMMAND_SENT = 2

local COM_QUERY = 0x03

local SERVER_MORE_RESULTS_EXISTS = 8

-- 16MB - 1, the default max allowed packet size used by libmysqlclient
local FULL_PACKET_SIZE = 16777215


local mt = { __index = _M }


-- mysql field value type converters
local converters = new_tab(0, 8)

for i = 0x01, 0x05 do
    -- tiny, short, long, float, double
    converters[i] = tonumber
end
converters[0x08] = tonumber  -- long long
converters[0x09] = tonumber  -- int24
converters[0x0d] = tonumber  -- year
converters[0xf6] = tonumber  -- newdecimal


local function _get_byte2(data, i)
	return strunpack("<I2",data,i)
end


local function _get_byte3(data, i)
	return strunpack("<I3",data,i)
end


local function _get_byte4(data, i)
	return strunpack("<I4",data,i)
end


local function _get_byte8(data, i)
	return strunpack("<I8",data,i)
end


local function _set_byte2(n)
    return strpack("<I2", n)
end


local function _set_byte3(n)
    return strpack("<I3", n)
end


local function _set_byte4(n)
    return strpack("<I4", n)
end


local function _from_cstring(data, i)
    return strunpack("z", data, i)
end

local function _dumphex(bytes)
	return strgsub(bytes, ".", function(x) return strformat("%02x ", strbyte(x)) end)
end


local function _compute_token(password, scramble)
    if password == "" then
        return ""
    end
    --_dumphex(scramble)

    local stage1 = sha1(password)
    --print("stage1:", _dumphex(stage1) )
    local stage2 = sha1(stage1)
    local stage3 = sha1(scramble .. stage2)

	local i = 0
	return strgsub(stage3,".",
		function(x)
			i = i + 1
			-- ~ is xor in lua 5.3
			return strchar(strbyte(x) ~ strbyte(stage1, i))
		end)
end

local function _compose_packet(self, req, size)
   self.packet_no = self.packet_no + 1

    local packet = _set_byte3(size) .. strchar(self.packet_no) .. req
    return packet
end

local function _recv_packet(self,sock)


    local data = sock:read( 4)
    if not data then
        return nil, nil, "failed to receive packet header: "
    end


    local len, pos = _get_byte3(data, 1)


    if len == 0 then
        return nil, nil, "empty packet"
    end

    if len > self._max_packet_size then
        return nil, nil, "packet size too big: " .. len
    end

    local num = strbyte(data, pos)

    self.packet_no = num

    data = sock:read(len)

    if not data then
        return nil, nil, "failed to read packet content: "
    end


    local field_count = strbyte(data, 1)
    local typ
    if field_count == 0x00 then
        typ = "OK"
    elseif field_count == 0xff then
        typ = "ERR"
    elseif field_count == 0xfe then
        typ = "EOF"
    else
        typ = "DATA"
    end

    return data, typ
end


local function _from_length_coded_bin(data, pos)
    local first = strbyte(data, pos)

    if not first then
        return nil, pos
    end

    if first >= 0 and first <= 250 then
        return first, pos + 1
    end

    if first == 251 then
        return nil, pos + 1
    end

    if first == 252 then
        pos = pos + 1
        return _get_byte2(data, pos)
    end

    if first == 253 then
        pos = pos + 1
        return _get_byte3(data, pos)
    end

    if first == 254 then
        pos = pos + 1
        return _get_byte8(data, pos)
    end

    return false, pos + 1
end


local function _from_length_coded_str(data, pos)
    local len
    len, pos = _from_length_coded_bin(data, pos)
    if len == nil then
        return nil, pos
    end

    return sub(data, pos, pos + len - 1), pos + len
end


local function _parse_ok_packet(packet)
    local res = new_tab(0, 5)
    local pos

    res.affected_rows, pos = _from_length_coded_bin(packet, 2)

    res.insert_id, pos = _from_length_coded_bin(packet, pos)

    res.server_status, pos = _get_byte2(packet, pos)

    res.warning_count, pos = _get_byte2(packet, pos)


    local message = sub(packet, pos)
    if message and message ~= "" then
        res.message = message
    end


    return res
end


local function _parse_eof_packet(packet)
    local pos = 2

    local warning_count, pos = _get_byte2(packet, pos)
    local status_flags = _get_byte2(packet, pos)

    return warning_count, status_flags
end


local function _parse_err_packet(packet)
    local errno, pos = _get_byte2(packet, 2)
    local marker = sub(packet, pos, pos)
    local sqlstate
    if marker == '#' then
        -- with sqlstate
        pos = pos + 1
        sqlstate = sub(packet, pos, pos + 5 - 1)
        pos = pos + 5
    end

    local message = sub(packet, pos)
    return errno, message, sqlstate
end


local function _parse_result_set_header_packet(packet)
    local field_count, pos = _from_length_coded_bin(packet, 1)

    local extra
    extra = _from_length_coded_bin(packet, pos)

    return field_count, extra
end


local function _parse_field_packet(data)
    local col = new_tab(0, 2)
    local catalog, db, table, orig_table, orig_name, charsetnr, length
    local pos
    catalog, pos = _from_length_coded_str(data, 1)


    db, pos = _from_length_coded_str(data, pos)
    table, pos = _from_length_coded_str(data, pos)
    orig_table, pos = _from_length_coded_str(data, pos)
    col.name, pos = _from_length_coded_str(data, pos)

    orig_name, pos = _from_length_coded_str(data, pos)

    pos = pos + 1 -- ignore the filler

    charsetnr, pos = _get_byte2(data, pos)

    length, pos = _get_byte4(data, pos)

    col.type = strbyte(data, pos)

    --[[
    pos = pos + 1

    col.flags, pos = _get_byte2(data, pos)

    col.decimals = strbyte(data, pos)
    pos = pos + 1

    local default = sub(data, pos + 2)
    if default and default ~= "" then
        col.default = default
    end
    --]]

    return col
end


local function _parse_row_data_packet(data, cols, compact)
    local pos = 1
    local ncols = #cols
    local row
    if compact then
        row = new_tab(ncols, 0)
    else
        row = new_tab(0, ncols)
    end
    for i = 1, ncols do
        local value
        value, pos = _from_length_coded_str(data, pos)
        local col = cols[i]
        local typ = col.type
        local name = col.name

        if value ~= nil then
            local conv = converters[typ]
            if conv then
                value = conv(value)
            end
        end

        if compact then
            row[i] = value

        else
            row[name] = value
        end
    end

    return row
end


local function _recv_field_packet(self, sock)
    local packet, typ, err = _recv_packet(self, sock)
    if not packet then
        return nil, err
    end

    if typ == "ERR" then
        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
    end

    if typ ~= 'DATA' then
        return nil, "bad field packet type: " .. typ
    end

    -- typ == 'DATA'

    return _parse_field_packet(packet)
end

local function _recv_decode_packet_resp(self)
     return function(sock)
		-- don't return more than 2 results
        return true, (_recv_packet(self,sock))
    end
end

local function _recv_auth_resp(self)
     return function(sock)
        local packet, typ, err = _recv_packet(self,sock)
        if not packet then
            --print("recv auth resp : failed to receive the result packet")
            error ("failed to receive the result packet"..err)
            --return nil,err
        end

        if typ == 'ERR' then
            local errno, msg, sqlstate = _parse_err_packet(packet)
            error( strformat("errno:%d, msg:%s,sqlstate:%s",errno,msg,sqlstate))
            --return nil, errno,msg, sqlstate
        end

        if typ == 'EOF' then
            error "old pre-4.1 authentication protocol not supported"
        end

        if typ ~= 'OK' then
            error "bad packet type: "
        end
        return true, true
    end
end


local function _mysql_login(self,user,password,database,on_connect)

    return function(sockchannel)
          local packet, typ, err =   sockchannel:response( _recv_decode_packet_resp(self) )
        --local aat={}
        if not packet then
            error(  err )
        end

        if typ == "ERR" then
            local errno, msg, sqlstate = _parse_err_packet(packet)
            error( strformat("errno:%d, msg:%s,sqlstate:%s",errno,msg,sqlstate))
        end

        self.protocol_ver = strbyte(packet)

        local server_ver, pos = _from_cstring(packet, 2)
        if not server_ver then
            error "bad handshake initialization packet: bad server version"
        end

        self._server_ver = server_ver


        local thread_id, pos = _get_byte4(packet, pos)

        local scramble1 = sub(packet, pos, pos + 8 - 1)
        if not scramble1 then
            error "1st part of scramble not found"
        end

        pos = pos + 9 -- skip filler

        -- two lower bytes
        self._server_capabilities, pos = _get_byte2(packet, pos)

        self._server_lang = strbyte(packet, pos)
        pos = pos + 1

        self._server_status, pos = _get_byte2(packet, pos)

        local more_capabilities
        more_capabilities, pos = _get_byte2(packet, pos)

        self._server_capabilities = self._server_capabilities|more_capabilities<<16

        local len = 21 - 8 - 1

        pos = pos + 1 + 10

        local scramble_part2 = sub(packet, pos, pos + len - 1)
        if not scramble_part2 then
            error "2nd part of scramble not found"
        end


        local scramble = scramble1..scramble_part2
        local token = _compute_token(password, scramble)

        local client_flags = 260047;

		local req = strpack("<I4I4c24zs1z",
			client_flags,
			self._max_packet_size,
			strrep("\0", 24),	-- TODO: add support for charset encoding
			user,
			token,
			database)

        local packet_len = #req

        local authpacket=_compose_packet(self,req,packet_len)
        sockchannel:request(authpacket,_recv_auth_resp(self))
	if on_connect then
		on_connect(self)
	end
    end
end


local function _compose_query(self, query)

    self.packet_no = -1

    local cmd_packet = strchar(COM_QUERY) .. query
    local packet_len = 1 + #query

    local querypacket = _compose_packet(self, cmd_packet, packet_len)
    return querypacket
end



local function read_result(self, sock)
    local packet, typ, err = _recv_packet(self, sock)
    if not packet then
        return nil, err
        --error( err )
    end

    if typ == "ERR" then
        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
        --error( strformat("errno:%d, msg:%s,sqlstate:%s",errno,msg,sqlstate))
    end

    if typ == 'OK' then
        local res = _parse_ok_packet(packet)
        if res and res.server_status&SERVER_MORE_RESULTS_EXISTS ~= 0 then
            return res, "again"
        end
        return res
    end

    if typ ~= 'DATA' then
        return nil, "packet type " .. typ .. " not supported"
        --error( "packet type " .. typ .. " not supported" )
    end

    -- typ == 'DATA'

    local field_count, extra = _parse_result_set_header_packet(packet)

    local cols = new_tab(field_count, 0)
    for i = 1, field_count do
        local col, err, errno, sqlstate = _recv_field_packet(self, sock)
        if not col then
            return nil, err, errno, sqlstate
            --error( strformat("errno:%d, msg:%s,sqlstate:%s",errno,msg,sqlstate))
        end

        cols[i] = col
    end

    local packet, typ, err = _recv_packet(self, sock)
    if not packet then
        --error( err)
        return nil, err
    end

    if typ ~= 'EOF' then
        --error ( "unexpected packet type " .. typ .. " while eof packet is ".. "expected" )
        return nil, "unexpected packet type " .. typ .. " while eof packet is ".. "expected"
    end

    -- typ == 'EOF'

    local compact = self.compact

    local rows = new_tab( 4, 0)
    local i = 0
    while true do
        packet, typ, err = _recv_packet(self, sock)
        if not packet then
            --error (err)
            return nil, err
        end

        if typ == 'EOF' then
            local warning_count, status_flags = _parse_eof_packet(packet)

            if status_flags&SERVER_MORE_RESULTS_EXISTS ~= 0 then
                return rows, "again"
            end

            break
        end

        -- if typ ~= 'DATA' then
            -- return nil, 'bad row packet type: ' .. typ
        -- end

        -- typ == 'DATA'

        local row = _parse_row_data_packet(packet, cols, compact)
        i = i + 1
        rows[i] = row
    end

    return rows
end

local function _query_resp(self)
     return function(sock)
        local res, err, errno, sqlstate = read_result(self,sock)
        if not res then
            local badresult ={}
            badresult.badresult = true
            badresult.err = err
            badresult.errno = errno
            badresult.sqlstate = sqlstate
            return true , badresult
        end
        if err ~= "again" then
            return true, res
        end
        local mulitresultset = {res}
        mulitresultset.mulitresultset = true
        local i =2
        while err =="again" do
            res, err, errno, sqlstate = read_result(self,sock)
            if not res then
                mulitresultset.badresult = true
                mulitresultset.err = err
                mulitresultset.errno = errno
                mulitresultset.sqlstate = sqlstate
                return true, mulitresultset
            end
            mulitresultset[i]=res
            i=i+1
        end
        return true, mulitresultset
    end
end

function _M.connect(opts)

    local self = setmetatable( {}, mt)

    local max_packet_size = opts.max_packet_size
    if not max_packet_size then
        max_packet_size = 1024 * 1024 -- default 1 MB
    end
    self._max_packet_size = max_packet_size
    self.compact = opts.compact_arrays


    local database = opts.database or ""
    local user = opts.user or ""
    local password = opts.password or ""

    local channel = socketchannel.channel {
        host = opts.host,
        port = opts.port or 3306,
        auth = _mysql_login(self,user,password,database,opts.on_connect),
    }
    self.sockchannel = channel
    -- try connect first only once
    channel:connect(true)


    return self
end



function _M.disconnect(self)
    self.sockchannel:close()
    setmetatable(self, nil)
end


function _M.query(self, query)
    local querypacket = _compose_query(self, query)
    local sockchannel = self.sockchannel
    if not self.query_resp then
        self.query_resp = _query_resp(self)
    end
    return  sockchannel:request( querypacket, self.query_resp )
end

function _M.server_ver(self)
    return self._server_ver
end


function _M.quote_sql_str( str)
    return mysqlaux.quote_sql_str(str)
end

function _M.set_compact_arrays(self, value)
    self.compact = value
end


return _M
