local crypt = require "crypt"
local openssl = require'openssl'
local csr,bio,ssl = openssl.csr,openssl.bio, openssl.ssl
local sslctx = require'openssl.sslctx'
local _,_,opensslv = openssl.version(true)
local _smtp = {}
local ctx = nil

function _smtp.init(cas, params)
    assert(type(cas) == "table", "unexpected root ca")
    assert(type(params)=="table", "unexpected param")
    local certstore = nil
    if opensslv > 0x10002000 then
        certstore = openssl.x509.store:new()
--        local cas = require'root_ca'
        for i=1,#cas do
          local cert = assert(openssl.x509.read(cas[i]))
          assert(certstore:add(cert))
        end
    end
    ctx = assert(sslctx.new(params))
    if certstore then
        ctx:cert_store(certstore)
    end
    ctx:verify_mode(ssl.peer, function (arg)
        return true
    end)
    ctx:set_cert_verify(function (arg)
        return true
    end)
end

_smtp.ip = ""
_smtp.port = 465
_smtp.sender = ""
_smtp.passwd = ""
_smtp.subject = ""
_smtp.fmt = ""

local function ask(sl, msg)
    print("ask", msg)
    sl:write(msg)
    print("answer", sl:read())
end

function _smtp.sendto(receiver, data)
    print("ssl connect to", _smtp.ip, _smtp.port)
    local cli = assert(bio.connect(_smtp.ip..':'.._smtp.port, true))
    if cli then
        local s = ctx:ssl(cli, false)
        assert(s:connect())
        local b,r = s:getpeerverification()
        assert(b)
        ask(s, string.format("ehlo %s\r\n", _smtp.ip))
        ask(s, "auth login\r\n")
        ask(s, crypt.base64encode(_smtp.sender).."\r\n")
        ask(s, crypt.base64encode(_smtp.passwd).."\r\n")
        ask(s, string.format("mail from:<%s>\r\n", _smtp.sender))
        ask(s, string.format("rcpt to:<%s>\r\n", receiver))
        ask(s, "data\r\n")
        ask(s, string.format("%s\r\n.\r\n", data))
        s:shutdown()
        cli:shutdown()
        cli:close()
        cli = nil
        collectgarbage()
    end
    openssl.error(true)
end

return _smtp

