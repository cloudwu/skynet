local openssl = require'openssl'
local ssl,pkey,x509 = openssl.ssl,openssl.pkey,openssl.x509

local M = {}

local function load(path)
    local f = assert(io.open(path,'r'))
    if f then
        local c = f:read('*all')
        f:close()
        return c
    end
end

function M.new(params)
    local protocol = params.protocol and string.upper(string.sub(params.protocol,1,3))
        ..string.sub(params.protocol,4,-1) or 'TLSv1_2'
    local ctx = ssl.ctx_new(protocol,params.ciphers)
    local xkey = nil
    if (type(params.password)=='nil') then
        xkey = assert(pkey.read(load(params.key),true,'pem'))
    elseif (type(params.password)=='string')  then
        xkey = assert(pkey.read(load(params.key),true,'pem',params.password))
    elseif (type(params.password)=='function') then
        local p = assert(params.password())
        xkey = assert(pkey.read(load(params.key),true,'pem',p))
    end

    assert(xkey)
    local xcert = nil
    if (params.certificate) then
        xcert = assert(x509.read(load(params.certificate)))
    end
    assert(ctx:use( xkey, xcert))

    if(params.cafile or params.capath) then
        ctx:verify_locations(params.cafile,params.capath)
    end

    unpack = unpack or table.unpack   
    if(params.verify) then
        ctx:verify_mode(params.verify)
    end
    if params.options then
        local args = {}
        for i=1,#params.options do
            table.insert(args,params.options[i])
        end
        ctx:options(unpack(args))
    end
    if params.verifyext then
        for k,v in pairs(params.verifyext) do
            params.verifyext[k] = string.gsub(v,'lsec_','')        
        end
        ctx:set_cert_verify(params.verifyext)
    end
    if params.dhparam then
        ctx:set_tmp('dh',params.dhparam)
    end
    if params.curve then
        ctx:set_tmp('ecdh',params.curve)
    end
    return ctx
end

return M
