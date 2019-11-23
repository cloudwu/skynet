local skynet = require "skynet"
local httpc = require "http.httpc"

local host = "http://127.0.0.1:8001"

-- 注意：用pcall防止coroutine异常退出
local function http_timeout()
    local respheader = {}
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(ok)

    local respheader = {}
    httpc.timeout = 10	-- set timeout 0.1s
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(not ok)

    local respheader = {}
    httpc.timeout = 1000	-- set timeout 10s
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(ok)
end

-- httpc.get(host, url, recvheader, header)
local function http_get()
    --　自定义header
    local respheader = {}
    local header = { host = "test.com" }
    local ok, status, body = pcall(httpc.get, host, "/echo", respheader, header)
    assert(ok)
    print(status, body)
end

-- httpc.post(host, url, form, recvheader)
local function http_post()
    --　自定义header
    local respheader = {}
    local form = {
        timestamp = skynet.now(),
        username = "xxx",
    }
    local ok, status, body = pcall(httpc.post, host, "/post", form, respheader)
    assert(ok)
    print(status, body)
end

local function escape(s)
    return (string.gsub(s, "([^A-Za-z0-9_])", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

function httpc.post_json(host, url, form)
    local header = {
        ["content-type"] = "application/json"
    }

    local body = {}
    for k,v in pairs(form) do
        table.insert(body, string.format("%s=%s", escape(k), escape(v)))
    end

    return httpc.request("POST", host, url, {}, header, table.concat(body , "&"))
end

-- httpc.request(method, host, url, recvheader, header, content)
local function http_post_json()
    local form = {
        timestamp = skynet.now(),
        username = "xxx",
    }

    local ok, status, body = pcall(httpc.post_json, host, "/post", form)
    assert(ok)
    print(status, body)
end

local function https_get()
    if not pcall(require,"ltls.c") then
    	print "No ltls module, https is not supported"
        return
    end

    local ok, status, body = pcall(httpc.get, "https://www.baidu.com", "/", respheader)
    assert(ok)
    print(status, #body)
end

skynet.start(function()
    skynet.newservice("websrv")
    http_get()
    http_timeout()
    http_post()
    http_post_json()
    https_get()
end)
