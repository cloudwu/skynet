local socket = require "http.sockethelper"
local c = require "ltls.c"

local tlshelper = {}

function tlshelper.init_requestfunc(fd, tls_ctx)
    local readfunc = socket.readfunc(fd)
    local writefunc = socket.writefunc(fd)
    return function ()
        local ds1 = tls_ctx:handshake()
        writefunc(ds1)
        while not tls_ctx:finished() do
            local ds2 = readfunc()
            local ds3 = tls_ctx:handshake(ds2)
            if ds3 then
                writefunc(ds3)
            end
        end
    end
end


function tlshelper.init_responsefunc(fd, tls_ctx)
    local readfunc = socket.readfunc(fd)
    local writefunc = socket.writefunc(fd)
    return function ()
        while not tls_ctx:finished() do
            local ds1 = readfunc()
            local ds2 = tls_ctx:handshake(ds1)
            if ds2 then
                writefunc(ds2)
            end
        end
        local ds3 = tls_ctx:write()
        writefunc(ds3)
    end
end

function tlshelper.closefunc(tls_ctx)
    return function ()
        tls_ctx:close()
    end
end

function tlshelper.readfunc(fd, tls_ctx)
    local readfunc = socket.readfunc(fd)
    local read_buff = ""
    return function (sz)
        if not sz then
            local s = ""
            if #read_buff == 0 then
                local ds = readfunc(sz)
                s = tls_ctx:read(ds)
            end
            s = read_buff .. s
            read_buff = ""
            return s
        else
            while #read_buff < sz do
                local ds = readfunc()
                local s = tls_ctx:read(ds)
                read_buff = read_buff .. s
            end
            local  s = string.sub(read_buff, 1, sz)
            read_buff = string.sub(read_buff, sz+1, #read_buff)
            return s
        end
    end
end

function tlshelper.writefunc(fd, tls_ctx)
    local writefunc = socket.writefunc(fd)
    return function (s)
        local ds = tls_ctx:write(s)
        return writefunc(ds)
    end
end

function tlshelper.readallfunc(fd, tls_ctx)
    local readfunc = socket.readfunc(fd)
    return function ()
        local ds = socket.readall(fd)
        local s = tls_ctx:read(ds)
        return s
    end
end

function tlshelper.newctx()
    return c.newctx()
end

function tlshelper.newtls(method, ssl_ctx)
    return c.newtls(method, ssl_ctx)
end

return tlshelper