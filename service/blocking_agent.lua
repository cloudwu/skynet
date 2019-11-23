local skynet = require "skynet"

local command = {}
function command.execute(cmdline)
    return os.execute(cmdline)
end

function command.popen(cmdline)
    local fp, err = io.popen(cmdline)
    if err then
        return nil, err
    end

    local s = fp:read("*a")
    fp:close()
    return s
end

function command.readfile(filename)
    local fp, err = io.open(filename, "rb")
    if err then
        return nil, err
    end
    local s = fp:read("*a")
    fp:close()
    return s
end

local function writefile(filename, mode, data)
    local fp, err = io.open(filename, mode)
    if err then
        return nil, err
    end

    _, err = fp:write(data)
    fp:close()
    if err then
        return nil, err
    end

    return true, nil
end

function command.writefile(filename, data)
    return writefile(filename, "w", data)
end

function command.appendfile(filename, data)
    return writefile(filename, "a+", data)
end


function command.open(filename, mode)
    local file, err = io.open(filename, mode)
end

function command.read(file, fmt)
    return file:read(fmt)
end

function command.seek(file, whence, offset)
    return file:seek(whence, offset)
end

function command.close(file)
    return file:close()
end

function command.write(file, data)
    return file:write(data)
end

function command.flush(file)
    return file:flush()
end

skynet.start(function()
    skynet.dispatch("lua", function (session, address, cmd, ...)
        local f = command[cmd]
        if not f then
            error(string.format("Unknown command %s", tostring(cmd)))
            return
        end

        skynet.ret(skynet.pack(f(...)))
    end)
end)