local crypt = require "skynet.crypt"

local NATIVE = "mysql_native_password"
local SHA2 = "caching_sha2_password"

local CLIENT_LONG_PASSWORD = 0x000001
local CLIENT_FOUND_ROWS = 0x000002
local CLIENT_LONG_FLAG = 0x000004
local CLIENT_CONNECT_WITH_DB = 0x000008
local CLIENT_IGNORE_SPACE = 0x000100
local CLIENT_PROTOCOL_41 = 0x000200
local CLIENT_INTERACTIVE = 0x000400
local CLIENT_TRANSACTIONS = 0x002000
local CLIENT_SECURE_CONNECTION = 0x008000
local CLIENT_MULTI_STATEMENTS = 0x010000
local CLIENT_MULTI_RESULTS = 0x020000
local CLIENT_PS_MULTI_RESULTS = 0x040000
local CLIENT_PLUGIN_AUTH = 0x080000

local CLIENT_FLAGS = CLIENT_LONG_PASSWORD | CLIENT_FOUND_ROWS | CLIENT_LONG_FLAG
    | CLIENT_IGNORE_SPACE | CLIENT_PROTOCOL_41 | CLIENT_INTERACTIVE | CLIENT_TRANSACTIONS
    | CLIENT_SECURE_CONNECTION | CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS
    | CLIENT_PS_MULTI_RESULTS | CLIENT_PLUGIN_AUTH

local function auth_token(plugin, password, nonce)
    if plugin ~= NATIVE and plugin ~= SHA2 then
        error("unsupported MySQL authentication plugin: " .. plugin)
    end
    assert(#nonce == 20, "invalid MySQL authentication challenge")
    if password == "" then
        return ""
    end
    if plugin == NATIVE then
        local first = crypt.sha1(password)
        return crypt.xor_str(first, crypt.sha1(nonce .. crypt.sha1(first)))
    end
    local digest = require "openssl.digest"
    local function sha256(data)
        return digest.new("sha256"):final(data)
    end
    local first = sha256(password)
    return crypt.xor_str(first, sha256(sha256(first) .. nonce))
end

local function full_auth(password, nonce, key_path, send)
    local pem
    if key_path then
        local file = assert(io.open(key_path, "rb"))
        local data, err = file:read("*a")
        file:close()
        pem = assert(data, err)
    else
        local reply = send("\2")
        assert(reply:byte() == 1, "expected MySQL RSA public key packet")
        pem = reply:sub(2)
    end
    local pkey = require "openssl.pkey"
    local key = pkey.new(pem, "PEM")
    assert(key:type() == "rsaEncryption", "expected an RSA server public key")
    return key:encrypt(crypt.xor_str(password .. "\0", nonce), { rsaPadding = pkey.RSA_PKCS1_OAEP_PADDING })
end

return function(self, user, password, charset, database, on_connect, key_path,
                compose_packet, recv_packet, parse_err_packet)
    return function(channel)
        -- A reconnect may have pending commands. Keep handshake sequencing local.
        local exchange = { packet_no = -1 }
        local function receive(sock)
            local expected = exchange.packet_no + 1
            local packet, typ, err = recv_packet(exchange, sock)
            assert(packet, err)
            assert(exchange.packet_no == expected, "unexpected MySQL authentication packet number")
            if typ == "ERR" then
                local errno, msg, state = parse_err_packet(packet)
                return false, string.format("MySQL %d (%s): %s", errno, state or "unknown", msg)
            end
            return true, packet
        end
        local function send(data)
            return channel:request(compose_packet(exchange, data), receive)
        end

        local packet = channel:response(receive)
        local pos
        self.protocol_ver, self._server_ver, pos = string.unpack("Bz", packet)
        assert(self.protocol_ver == 10, "unsupported MySQL handshake protocol")
        local first, low, high, length
        first, low, self._server_lang, self._server_status, high, length, pos =
            string.unpack("<xxxxc8xI2BI2I2Bxxxxxxxxxx", packet, pos)
        local capabilities = low | high << 16
        self._server_capabilities = capabilities
        local required = CLIENT_SECURE_CONNECTION | CLIENT_PROTOCOL_41
        assert(capabilities & required == required, "pre-4.1 MySQL authentication is unsupported")
        local nonce = first .. packet:sub(pos, pos + 11)
        local advertised_plugin
        if capabilities & CLIENT_PLUGIN_AUTH ~= 0 then
            advertised_plugin = packet:sub(pos + math.max(13, length - 8)):gsub("%z$", "")
        end
        -- The greeting describes the server default. The account may select
        -- another plugin in an AuthSwitchRequest after the first response.
        local plugin = NATIVE
        if advertised_plugin == SHA2 and (password == "" or pcall(require, "openssl.digest")) then
            plugin = SHA2
        end

        local flags = CLIENT_FLAGS
        if database ~= "" then flags = flags | CLIENT_CONNECT_WITH_DB end
        flags = flags & capabilities
        if capabilities >> 16 == 0 then
            flags = flags | CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS
        end
        local header = string.pack("<I4I4B", flags, self._max_packet_size, charset) .. string.rep("\0", 23)
        local request = header .. string.pack("zs1", user, auth_token(plugin, password, nonce))
        if flags & CLIENT_CONNECT_WITH_DB ~= 0 then request = request .. string.pack("z", database) end
        if flags & CLIENT_PLUGIN_AUTH ~= 0 then request = request .. string.pack("z", plugin) end

        packet = send(request)
        if packet:byte() == 0xfe then
            assert(flags & CLIENT_PLUGIN_AUTH ~= 0 and #packet > 1, "old MySQL password authentication is unsupported")
            local pos
            plugin, pos = string.unpack("z", packet, 2)
            nonce = packet:sub(pos)
            if #nonce == 21 and nonce:byte(21) == 0 then nonce = nonce:sub(1, 20) end
            packet = send(auth_token(plugin, password, nonce))
        end
        if plugin == SHA2 then
            if packet == "\1\3" then
                packet = channel:response(receive)
            elseif packet == "\1\4" then
                packet = send(full_auth(password, nonce, key_path, send))
            end
        end
        assert(packet:byte() == 0 and #packet >= 7, "expected final MySQL authentication OK")
        if database ~= "" and flags & CLIENT_CONNECT_WITH_DB == 0 then
            local result = self:query("USE `" .. database:gsub("`", "``") .. "`")
            assert(not result.badresult, result.err)
        end
        if on_connect then on_connect(self) end
    end
end
