-- lua-resty-rabbitmqstomp: Opinionated RabbitMQ (STOMP) client lib
-- Copyright (C) 2013 Rohit 'bhaisaab' Yadav, Wingify
-- Opensourced at Wingify in New Delhi under the MIT License


local byte = string.byte
local concat = table.concat
local error = error
local find = string.find
local gsub = string.gsub
local insert = table.insert
local len = string.len
local pairs = pairs
local setmetatable = setmetatable
local sub = string.sub

local skynet = require "skynet"
local socketchannel =  require "skynet.socketchannel"


local _M = {
    _VERSION = "0.1",
}
_M.__index = _M

local mt = { __index = _M }


local LF = "\x0a"
local EOL = "\x0d\x0a"
local NULL_BYTE = "\x00"


function _M:_build_frame(command, headers, body)
    local frame = {command, EOL}

    if body then
        headers["content-length"] = len(body)
    end

    for key, value in pairs(headers) do
        insert(frame, key)
        insert(frame, ":")
        insert(frame, value)
        insert(frame, EOL)
    end

    insert(frame, EOL)

    if body then
        insert(frame, body)
    end

    insert(frame, NULL_BYTE)
    insert(frame, EOL)
    return concat(frame, "")
end

local function __dispatch_resp(self)
    return function (sock)
        local frame = nil
        if self.opts.trailing_lf == nil or self.opts.trailing_lf == true then
            frame = sock:readline(NULL_BYTE .. LF)
        else
            frame = sock:readline(NULL_BYTE)
        end

        if not frame then
            return false
        end

        -- We successfully received a frame, but it was an ERROR frame
        if sub(frame, 1, len('ERROR') ) == 'ERROR' then
            skynet.error("rabbitmq error:", frame)
        end
        return true, frame
    end
end

function _M:_send_frame(frame)
    local dispatch_resp = __dispatch_resp(self)
    return self.__sock:request(frame, dispatch_resp)
end

local function rabbitmq_login(self)
    return function(sc)
        local headers = {}
        headers["accept-version"] = "1.2"
        headers["login"] = self.opts.username
        headers["passcode"] = self.opts.password
        headers["host"] = self.opts.vhost
    
        return self:_send_frame(self:_build_frame("CONNECT", headers, nil))
    end
end

function _M.connect(conf, opts)
    if opts == nil then
        opts = {username = "guest", password = "guest", vhost = "/", trailing_lf = true}
    end
    
    local obj = {
        opts = opts,
    }

    obj.__sock = socketchannel.channel {
        auth = rabbitmq_login(obj),
        host = conf.host or "127.0.0.1",
        port = conf.port or 61613,
        nodelay = true,
        overload = conf.overload,
    }
    
    setmetatable(obj, _M)
    obj.__sock:connect(true)
    return obj
end

function _M:send(smsg, headers)
    local f = nil
    if headers["receipt"] ~= nil then
        f = __dispatch_resp(self)
    end
    return self.__sock:request(self:_build_frame("SEND", headers, smsg), f)
end

function _M:subscribe(headers, cb)
    self.__cb = cb 
    return self:_send_frame(self:_build_frame("SUBSCRIBE", headers))
end

function _M:unsubscribe(headers)
    return self:_send_frame(self:_build_frame("UNSUBSCRIBE", headers))
end

function _M:receive()
    local so = self.__sock
    while so do
        local dispatch_resp = __dispatch_resp(self)
        local data, err = so:response(dispatch_resp)

        if not data then
            return nil, err
        end
        
        local idx = find(data, "\n\n", 2)
        self.__cb(sub(data, idx + 2))
    end
end

function _M:close()
    if self.state then
        -- Graceful shutdown
        local headers = {}
        headers["receipt"] = "disconnect"
        self:_send_frame(self:_build_frame("DISCONNECT", headers, nil))
    end

    self.__sock:close()
    setmetatable(self, nil)
end


return _M
