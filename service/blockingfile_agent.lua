local skynet = require "skynet"

local command = {}
local associate = {}    -- {handle_str = file_handle}

function command.open(filename, mode)
    local file, err = io.open(filename, mode)
    if err then
        return nil, err
    end

    local addr = tostring(file)
    associate[addr] = file

    return addr, nil
end

function command.read(addr, fmt)
    local file = assert(associate[addr])
    return file:read(fmt)
end

function command.seek(addr, whence, offset)
    local file = assert(associate[addr])
    return file:seek(whence, offset)
end

function command.close(addr)
    local file = assert(associate[addr])
    associate[addr] = nil
    return file:close()
end

function command.write(addr, data)
    local file = assert(associate[addr])
    local _, err = file:write(data)
    if err then
        return nil, err
    end
    return addr
end

function command.flush(addr)
    local file = assert(associate[addr])
    return file:flush()
end

skynet.start(function()
    skynet.dispatch("lua", function (session, address, cmd, ...)
        local f = command[cmd]
        if not f then
            error(string.format("Unknown command %s", tostring(cmd)))
            return
        end

        skynet.retpack(f(...))
    end)
end)