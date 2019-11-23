local skynet = require "skynet"
local socket = require "skynet.socket"
local httpd = require "http.httpd"
local sockethelper = require "http.sockethelper"
local urllib = require "http.url"
local table = table
local string = string

local handler = {}
handler["/timestamp"] = function(method, header, query, body)
    local params = urllib.parse_query(query)
    local timeout = tonumber(params.timeout)
    if timeout then
        skynet.sleep(100*timeout)
    end
    return 200, tostring(os.time())
end

handler["/post"] = function(method, header, query, body)
    if method ~= "POST" then
        return 404, "bad request"
    end

    local tmp = {}
    for k,v in pairs(header) do
        table.insert(tmp, string.format("%s = %s",k,v))
    end

    table.insert(tmp, string.format("body = %s", body))
    return 200, table.concat(tmp,"\n")
end

handler["/echo"] = function(method, header, query, body)
    local tmp = {
        string.format("method: %s", method),
    }

    table.insert(tmp, string.format("host: %s", header.host))

    if query then
        local q = urllib.parse_query(query)
        for k, v in pairs(q) do
            table.insert(tmp, string.format("query: %s = %s", k,v))
        end
    end

    for k,v in pairs(header) do
        table.insert(tmp, string.format("%s = %s",k,v))
    end

    table.insert(tmp, string.format("body = %s", body))
    return 200, table.concat(tmp,"\n")
end

skynet.start(function()
    skynet.dispatch("lua", function(session, address, id)
        socket.start(id)  --　开始读取数据　

        local read = sockethelper.readfunc(id)
        local write = sockethelper.writefunc(id)

        -- 读取请求
        local code, url, method, header, body = httpd.read_request(read, nil)
        if not code then    --　错误处理
            if url == sockethelper.socket_error then
                skynet.error("socket closed")
            else
                skynet.error(url)
            end
            return socket.close(id) --　关闭连接　
        end

        if code ~= 200 then
            return httpd.write_response(write, 400, "invalid request")
        end

        local path, query = urllib.parse(url)
        local f = handler[path]
        if not f then
            return httpd.write_response(write, 400, "invalid request")
        end

        local code, data = f(method, header, query, body) --　处理请求
        return httpd.write_response(write, code, data)          --　回复结果
    end)
end)

