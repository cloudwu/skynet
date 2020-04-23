local skynet = require "skynet"
local crypt = require "skynet.crypt"

local text = "hello world"
local key = "12345678"

local function desencode(key, text, padding)
	local c = crypt.desencode(key, text, crypt.padding[padding or "iso7816_4"])
	return crypt.base64encode(c)
end

local function desdecode(key, text, padding)
	text = crypt.base64decode(text)
	return crypt.desdecode(key, text, crypt.padding[padding or "iso7816_4"])
end

local etext = desencode(key, text)
assert( etext == "KNugLrX23UcGtcVlk9y+LA==")
assert(desdecode(key, etext) == text)

local etext = desencode(key, text, "pkcs7")
assert(desdecode(key, etext, "pkcs7") == text)

skynet.start(skynet.exit)