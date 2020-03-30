local internal = require "http.internal"
local socket = require "skynet.socket"
local crypt = require "skynet.crypt"
local httpd = require "http.httpd"
local skynet = require "skynet"
local sockethelper = require "http.sockethelper"
local socket_error = sockethelper.socket_error

local GLOBAL_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
local MAX_FRAME_SIZE = 256 * 1024 -- max frame is 256K

local M = {}


local ws_pool = {}
local function _close_websocket(ws_obj)
    local id = ws_obj.id
    assert(ws_pool[id] == ws_obj)
    ws_pool[id] = nil
    ws_obj.close()
end

local function _isws_closed(id)
    return not ws_pool[id]
end


local function write_handshake(self, host, url, header)
    local key = crypt.base64encode(crypt.randomkey()..crypt.randomkey())
    local request_header = {
        ["Upgrade"] = "websocket",
        ["Connection"] = "Upgrade",
        ["Sec-WebSocket-Version"] = "13",
        ["Sec-WebSocket-Key"] = key
    }
    if header then
        for k,v in pairs(header) do
            assert(request_header[k] == nil, k)
            request_header[k] = v
        end
    end

    local recvheader = {}
    local code, body = internal.request(self, "GET", host, url, recvheader, request_header)
    if code ~= 101 then
        error(string.format("websocket handshake error: code[%s] info:%s", code, body))    
    end

    if not recvheader["upgrade"] or recvheader["upgrade"]:lower() ~= "websocket" then
        error("websocket handshake upgrade must websocket")
    end

    if not recvheader["connection"] or recvheader["connection"]:lower() ~= "upgrade" then
        error("websocket handshake connection must upgrade")
    end

    local sw_key = recvheader["sec-websocket-accept"]
    if not sw_key then
        error("websocket handshake need Sec-WebSocket-Accept")
    end

    local guid = self.guid
    sw_key = crypt.base64decode(sw_key)
    if sw_key ~= crypt.sha1(key .. guid) then
        error("websocket handshake invalid Sec-WebSocket-Accept")
    end
end


local function read_handshake(self)
    local tmpline = {}
    local header_body = internal.recvheader(self.read, tmpline, "")
    if not header_body then
        return 413
    end

    local request = assert(tmpline[1])
    local method, url, httpver = request:match "^(%a+)%s+(.-)%s+HTTP/([%d%.]+)$"
    assert(method and url and httpver)
    if method ~= "GET" then
        return 400, "need GET method"
    end

    httpver = assert(tonumber(httpver))
    if httpver < 1.1 then
        return 505  -- HTTP Version not supported
    end

    local header = internal.parseheader(tmpline, 2, {})
    if not header then
        return 400  -- Bad request
    end
    if not header["upgrade"] or header["upgrade"]:lower() ~= "websocket" then
        return 426, "Upgrade Required"
    end

    if not header["host"] then
        return 400, "host Required"
    end

    if not header["connection"] or not header["connection"]:lower():find("upgrade", 1,true) then
        return 400, "Connection must Upgrade"
    end

    local sw_key = header["sec-websocket-key"]
    if not sw_key then
        return 400, "Sec-WebSocket-Key Required"
    else
        local raw_key = crypt.base64decode(sw_key)
        if #raw_key ~= 16 then
            return 400, "Sec-WebSocket-Key invalid"
        end
    end

    if not header["sec-websocket-version"] or header["sec-websocket-version"] ~= "13" then
        return 400, "Sec-WebSocket-Version must 13"
    end

    local sw_protocol = header["sec-websocket-protocol"]
    local sub_pro = ""
    if sw_protocol then
        local has_chat = false
        for sub_protocol in string.gmatch(sw_protocol, "[^%s,]+") do
            if sub_protocol == "chat" then
                sub_pro = "Sec-WebSocket-Protocol: chat\r\n"
                has_chat = true
                break
            end
        end
        if not has_chat then
            return 400, "Sec-WebSocket-Protocol need include chat"
        end
    end

    -- response handshake
    local accept = crypt.base64encode(crypt.sha1(sw_key .. self.guid))
    local resp = "HTTP/1.1 101 Switching Protocols\r\n"..
                 "Upgrade: websocket\r\n"..
                 "Connection: Upgrade\r\n"..
    string.format("Sec-WebSocket-Accept: %s\r\n", accept)..
                  sub_pro ..
                  "\r\n"
    self.write(resp)
    return nil, header, url
end

local function try_handle(self, method, ...)
    local handle = self.handle
    local f = handle and handle[method]
    if f then
        f(self.id, ...)
    end
end

local op_code = {
    ["frame"]  = 0x00,
    ["text"]   = 0x01,
    ["binary"] = 0x02,
    ["close"]  = 0x08,
    ["ping"]   = 0x09,
    ["pong"]   = 0x0A,
    [0x00]     = "frame",
    [0x01]     = "text",
    [0x02]     = "binary",
    [0x08]     = "close",
    [0x09]     = "ping",
    [0x0A]     = "pong",
}

local function write_frame(self, op, payload_data, masking_key)
    payload_data = payload_data or ""
    local payload_len = #payload_data
    local op_v = assert(op_code[op])
    local v1 = 0x80 | op_v -- fin is 1 with opcode
    local s
    local mask = masking_key and 0x80 or 0x00
    -- mask set to 0
    if payload_len < 126 then
        s = string.pack("I1I1", v1, mask | payload_len)
    elseif payload_len < 0xffff then
        s = string.pack("I1I1>I2", v1, mask | 126, payload_len)
    else
        s = string.pack("I1I1>I8", v1, mask | 127, payload_len)
    end
    self.write(s)

    -- write masking_key
    if masking_key then
        s = string.pack(">I4", masking_key)
        self.write(s)
        payload_data = crypt.xor_str(payload_data, s)
    end

    if payload_len > 0 then
        self.write(payload_data)
    end
end


local function read_close(payload_data)
    local code, reason
    local payload_len = #payload_data
    if payload_len > 2 then
        local fmt = string.format(">I2c%d", payload_len - 2)
        code, reason = string.unpack(fmt, payload_data)
    end
    return code, reason
end


local function read_frame(self)
    local s = self.read(2)
    local v1, v2 = string.unpack("I1I1", s)
    local fin  = (v1 & 0x80) ~= 0
    -- unused flag
    -- local rsv1 = (v1 & 0x40) ~= 0
    -- local rsv2 = (v1 & 0x20) ~= 0
    -- local rsv3 = (v1 & 0x10) ~= 0
    local op   =  v1 & 0x0f
    local mask = (v2 & 0x80) ~= 0
    local payload_len = (v2 & 0x7f)
    if payload_len == 126 then
        s = self.read(2)
        payload_len = string.unpack(">I2", s)
    elseif payload_len == 127 then
        s = self.read(8)
        payload_len = string.unpack(">I8", s)
    end

    if self.mode == "server" and payload_len > MAX_FRAME_SIZE then
        error("payload_len is too large")
    end

    -- print(string.format("fin:%s, op:%s, mask:%s, payload_len:%s", fin, op_code[op], mask, payload_len))
    local masking_key = mask and self.read(4) or false
    local payload_data = payload_len>0 and self.read(payload_len) or ""
    payload_data = masking_key and crypt.xor_str(payload_data, masking_key) or payload_data
    return fin, assert(op_code[op]), payload_data
end


local function resolve_accept(self)
    try_handle(self, "connect")
    local code, err, url = read_handshake(self)
    if code then
        local ok, s = httpd.write_response(self.write, code, err)
        if not ok then
            error(s)
        end
        try_handle(self, "close")
        return
    end

    local header = err
    try_handle(self, "handshake", header, url)
    local recv_count = 0
    local recv_buf = {}
    while true do
        if _isws_closed(self.id) then
            try_handle(self, "close")
            return
        end
        local fin, op, payload_data = read_frame(self)
        if op == "close" then
            local code, reason = read_close(payload_data)
            write_frame(self, "close")
            try_handle(self, "close", code, reason)
            break
        elseif op == "ping" then
            write_frame(self, "pong", payload_data)
            try_handle(self, "ping")
        elseif op == "pong" then
            try_handle(self, "pong")
        else
            if fin and #recv_buf == 0 then
                try_handle(self, "message", payload_data, op)
            else
                recv_buf[#recv_buf+1] = payload_data
                recv_count = recv_count + #payload_data
                if recv_count > MAX_FRAME_SIZE then
                    error("payload_len is too large")
                end
                if fin then
                    local s = table.concat(recv_buf)
                    try_handle(self, "message", s, op)
                    recv_buf = {}  -- clear recv_buf
                    recv_count = 0
                end
            end
        end
    end
end


local SSLCTX_CLIENT = nil
local function _new_client_ws(socket_id, protocol)
    local obj
    if protocol == "ws" then
        obj = {
            websocket = true,
            close = function ()
                socket.close(socket_id)
            end,
            read = sockethelper.readfunc(socket_id),
            write = sockethelper.writefunc(socket_id),
            readall = function ()
                return socket.readall(socket_id)
            end,
        }
    elseif protocol == "wss" then
        local tls = require "http.tlshelper"
        SSLCTX_CLIENT = SSLCTX_CLIENT or tls.newctx()
        local tls_ctx = tls.newtls("client", SSLCTX_CLIENT)
        local init = tls.init_requestfunc(socket_id, tls_ctx)
        init()
        obj = {
            websocket = true,
            close = function ()
                socket.close(socket_id)
                tls.closefunc(tls_ctx)() 
            end,
            read = tls.readfunc(socket_id, tls_ctx),
            write = tls.writefunc(socket_id, tls_ctx),
            readall = tls.readallfunc(socket_id, tls_ctx),
        }
    else
        error(string.format("invalid websocket protocol:%s", tostring(protocol)))
    end

    obj.mode = "client"
    obj.id = assert(socket_id)
    obj.guid = GLOBAL_GUID
    ws_pool[socket_id] = obj
    return obj
end


local SSLCTX_SERVER = nil
local function _new_server_ws(socket_id, handle, protocol)
    local obj
    if protocol == "ws" then
        obj = {
            close = function ()
                socket.close(socket_id)
            end,
            read = sockethelper.readfunc(socket_id),
            write = sockethelper.writefunc(socket_id),
        }

    elseif protocol == "wss" then
        local tls = require "http.tlshelper"
        if not SSLCTX_SERVER then
            SSLCTX_SERVER = tls.newctx()
            -- gen cert and key
            -- openssl req -x509 -newkey rsa:2048 -days 3650 -nodes -keyout server-key.pem -out server-cert.pem
            local certfile = skynet.getenv("certfile") or "./server-cert.pem"
            local keyfile = skynet.getenv("keyfile") or "./server-key.pem"
            SSLCTX_SERVER:set_cert(certfile, keyfile)
        end
        local tls_ctx = tls.newtls("server", SSLCTX_SERVER)
        local init = tls.init_responsefunc(socket_id, tls_ctx)
        init()
        obj = {
            close = function ()
                socket.close(socket_id)
                tls.closefunc(tls_ctx)() 
            end,
            read = tls.readfunc(socket_id, tls_ctx),
            write = tls.writefunc(socket_id, tls_ctx),
        }

    else
        error(string.format("invalid websocket protocol:%s", tostring(protocol)))
    end

    obj.mode = "server"
    obj.id = assert(socket_id)
    obj.handle = handle
    obj.guid = GLOBAL_GUID
    ws_pool[socket_id] = obj
    return obj
end


-- handle interface
-- connect / handshake / message / ping / pong / close / error
function M.accept(socket_id, handle, protocol, addr)
    socket.start(socket_id)
    protocol = protocol or "ws"
    local ws_obj = _new_server_ws(socket_id, handle, protocol)
    ws_obj.addr = addr
    local on_warning = handle and handle["warning"]
    if on_warning then
        socket.warning(socket_id, function (id, sz)
            on_warning(ws_obj, sz)
        end)
    end

    local ok, err = xpcall(resolve_accept, debug.traceback, ws_obj)
    local closed = _isws_closed(socket_id)
    if not closed then
        _close_websocket(ws_obj)
    end
    if not ok then
        if err == socket_error then
            if closed then
                try_handle(ws_obj, "close")
            else
                try_handle(ws_obj, "error")
            end
        else
            -- error(err)
            return false, err
        end
    end
    return true
end


function M.connect(url, header, timeout)
    local protocol, host, uri = string.match(url, "^(wss?)://([^/]+)(.*)$")
    if protocol ~= "wss" and protocol ~= "ws" then
        error(string.format("invalid protocol: %s", protocol))
    end
    
    assert(host)
    local host_name, host_port = string.match(host, "^([^:]+):?(%d*)$")
    assert(host_name and host_port)
    if host_port == "" then
        host_port = protocol == "ws" and 80 or 443
    end

    uri = uri == "" and "/" or uri
    local socket_id = sockethelper.connect(host_name, host_port, timeout)
    local ws_obj = _new_client_ws(socket_id, protocol)
    ws_obj.addr = host
    write_handshake(ws_obj, host_name, uri, header)
    return socket_id
end


function M.read(id)
    local ws_obj = assert(ws_pool[id])
    local recv_buf
    while true do
        local fin, op, payload_data = read_frame(ws_obj)
        if op == "close" then
            _close_websocket(ws_obj)
            return false, payload_data
        elseif op == "ping" then
            write_frame(ws_obj, "pong", payload_data)
        elseif op ~= "pong" then  -- op is frame, text binary
            if fin and not recv_buf then
                return payload_data
            else
                recv_buf = recv_buf or {}
                recv_buf[#recv_buf+1] = payload_data
                if fin then
                    local s = table.concat(recv_buf)
                    return s
                end
            end
        end
    end
    assert(false)
end


function M.write(id, data, fmt, masking_key)
    local ws_obj = assert(ws_pool[id])
    fmt = fmt or "text"
    assert(fmt == "text" or fmt == "binary")
    write_frame(ws_obj, fmt, data, masking_key)
end


function M.ping(id)
    local ws_obj = assert(ws_pool[id])
    write_frame(ws_obj, "ping")
end

function M.addrinfo(id)
    local ws_obj = assert(ws_pool[id])
    return ws_obj.addr
end

function M.close(id, code ,reason)
    local ws_obj = ws_pool[id]
    if not ws_obj then
        return
    end

    local ok, err = xpcall(function ()
        reason = reason or ""
        local payload_data
        if code then
            local fmt =string.format(">I2c%d", #reason)
            payload_data = string.pack(fmt, code, reason)
        end
        write_frame(ws_obj, "close", payload_data)
    end, debug.traceback)
    _close_websocket(ws_obj)
    if not ok then
        skynet.error(err)
    end
end


return M
