-- Run from the repository root: ./3rd/lua/lua test/testwebsocket.lua
-- Isolate the transport and handshake dependencies; exercise the public API.
local GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

local function xor(data, key)
    local result = {}
    for i = 1, #data do
        result[i] = string.char(data:byte(i) ~ key:byte((i - 1) % #key + 1))
    end
    return table.concat(result)
end

local function incoming_frame(op, payload, masked)
    assert(#payload < 126)
    if masked then
        local key = "\x12\x34\x56\x78"
        return string.char(0x80 | op, 0x80 | #payload) .. key .. xor(payload, key)
    end
    return string.char(0x80 | op, #payload) .. payload
end

local function new_fixture(input)
    local transport = { input = input or "", output = {}, random_calls = 0 }
    local function identity(value)
        return value
    end
    local modules = {
        ["skynet"] = { error = error },
        ["skynet.socket"] = {
            close = function() transport.closed = true end,
        },
        ["skynet.crypt"] = {
            randomkey = function()
                transport.random_calls = transport.random_calls + 1
                return string.pack(">I4I4", transport.random_calls, 0x12345678)
            end,
            xor_str = xor,
            -- Hashing and base64 are irrelevant to these frame tests.
            base64encode = identity,
            base64decode = identity,
            sha1 = identity,
        },
        ["http.internal"] = {
            request = function(_, _, _, _, response, request)
                response.upgrade = "websocket"
                response.connection = "Upgrade"
                response["sec-websocket-accept"] = request["Sec-WebSocket-Key"] .. GUID
                return 101, ""
            end,
        },
        ["http.httpd"] = {},
        ["http.sockethelper"] = {
            connect = function() return 1 end,
            readfunc = function()
                return function(size)
                    assert(#transport.input >= size, "unexpected read")
                    local data = transport.input:sub(1, size)
                    transport.input = transport.input:sub(size + 1)
                    return data
                end
            end,
            writefunc = function()
                return function(data)
                    transport.output[#transport.output + 1] = data
                end
            end,
        },
    }
    local env = setmetatable({
        require = function(name) return assert(modules[name], name) end,
    }, { __index = _G })
    local websocket = assert(loadfile("lualib/http/websocket.lua", "t", env))()
    return websocket, transport
end

local function client(input)
    local websocket, transport = new_fixture(input)
    local id = websocket.connect("ws://127.0.0.1/test")
    -- Exclude the HTTP handshake's random calls from the frame assertions.
    transport.random_calls = 0
    return websocket, id, transport
end

local function check_frame(transport, op, payload, key)
    local data = table.concat(transport.output)
    transport.output = {}
    local first, second, pos = string.unpack("BB", data)
    assert(first == 0x80 | op, "incorrect FIN or opcode")
    assert((second & 0x80 ~= 0) == (key ~= nil), "incorrect MASK bit")
    local size = second & 0x7f
    if size == 126 then
        size, pos = string.unpack(">I2", data, pos)
    elseif size == 127 then
        size, pos = string.unpack(">I8", data, pos)
    end
    assert(size == #payload, "incorrect payload length")
    if key then
        local actual
        actual, pos = string.unpack(">I4", data, pos)
        assert(actual == key, "incorrect masking key")
        payload = xor(payload, string.pack(">I4", key))
    end
    assert(data:sub(pos) == payload, "incorrect encoded payload")
end

local cases = 0
local function test(name, f)
    f()
    cases = cases + 1
    print("PASS: " .. name)
end

test("client text and binary payload length boundaries", function()
    local websocket, id, transport = client()
    local count = 0
    for _, fmt in ipairs { "text", "binary" } do
        for _, size in ipairs { 0, 125, 126, 65535, 65536 } do
            local payload = ("\0\xffhello"):rep(size // 7 + 1):sub(1, size)
            websocket.write(id, payload, fmt)
            count = count + 1
            check_frame(transport, fmt == "text" and 1 or 2, payload, count)
            assert(transport.random_calls == count)
        end
    end
end)

test("false selects automatic client masking", function()
    local websocket, id, transport = client()
    websocket.write(id, "hello", "text", false)
    assert(transport.random_calls == 1)
    check_frame(transport, 1, "hello", 1)
end)

test("explicit client keys, including zero", function()
    local websocket, id, transport = client()
    for _, key in ipairs { 0, 0x37fa213d, 0xffffffff } do
        websocket.write(id, "Hello", "text", key)
        assert(transport.random_calls == 0)
        if key == 0x37fa213d then
            -- RFC 6455 section 5.7's masked Hello example.
            assert(table.concat(transport.output) ==
                "\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58")
        end
        check_frame(transport, 1, "Hello", key)
    end
end)

test("client ping gets a fresh key for each frame", function()
    local websocket, id, transport = client()
    for key = 1, 2 do
        websocket.ping(id)
        assert(transport.random_calls == key)
        check_frame(transport, 9, "", key)
    end
end)

test("client automatically masks pong and preserves ping data", function()
    local payload = "\0\xffping"
    local websocket, id, transport = client(
        incoming_frame(9, payload) .. incoming_frame(1, "reply"))
    assert(websocket.read(id) == "reply")
    assert(transport.random_calls == 1)
    check_frame(transport, 10, payload, 1)
end)

test("client close with and without a status code", function()
    for _, with_code in ipairs { false, true } do
        local websocket, id, transport = client()
        local payload = ""
        if with_code then
            websocket.close(id, 1000, "bye")
            payload = string.pack(">I2", 1000) .. "bye"
        else
            websocket.close(id)
        end
        assert(transport.random_calls == 1)
        check_frame(transport, 8, payload, 1)
        assert(transport.closed and websocket.is_close(id))
    end
end)

test("server data and control frames remain unmasked by default", function()
    local payload = "server ping"
    local websocket, transport = new_fixture(
        incoming_frame(9, payload, true) .. incoming_frame(8, "", true))
    local handle = {
        handshake = function(id)
            transport.output = {} -- Discard the HTTP upgrade response.
            websocket.write(id, "hello")
            check_frame(transport, 1, "hello")
            websocket.write(id, "\0\xff", "binary")
            check_frame(transport, 2, "\0\xff")
            websocket.ping(id)
            check_frame(transport, 9, "")
            -- Preserve the existing explicit-key API in server mode too.
            websocket.write(id, "explicit", "text", 0)
            check_frame(transport, 1, "explicit", 0)
        end,
        ping = function()
            check_frame(transport, 10, payload)
        end,
        close = function()
            check_frame(transport, 8, "")
        end,
    }
    local ok, err = websocket.accept(1, handle, "ws", nil, { upgrade = {
        method = "GET", url = "/test", header = {
            host = "localhost", upgrade = "websocket", connection = "Upgrade",
            ["sec-websocket-version"] = "13",
            ["sec-websocket-key"] = "0123456789abcdef",
        },
    } })
    assert(ok, err)
    assert(transport.random_calls == 0)
    assert(transport.closed and transport.input == "")
end)

print(string.format("websocket masking tests passed (%d cases)", cases))
