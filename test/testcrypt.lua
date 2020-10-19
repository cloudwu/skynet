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

assert(desencode(key, "","pkcs7")=="/rlZt9RkL8s=")
assert(desencode(key, "1","pkcs7")=="g6AtgJul6q0=")
assert(desencode(key, "12","pkcs7")=="NefFpG+m1O4=")
assert(desencode(key, "123","pkcs7")=="LDiFUdf0iew=")
assert(desencode(key, "1234","pkcs7")=="T9u7dzBdi+w=")
assert(desencode(key, "12345","pkcs7")=="AGgKdx/Qic8=")
assert(desencode(key, "123456","pkcs7")=="ED5wLgc3Mnw=")
assert(desencode(key, "1234567","pkcs7")=="mYo+BYIT41M=")
assert(desencode(key, "12345678","pkcs7")=="ltACiHjVjIn+uVm31GQvyw==")

skynet.start(skynet.exit)