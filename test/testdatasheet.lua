local skynet = require "skynet"

local mode = ...

local function dump(t, prefix)
	for k,v in pairs(t) do
		print(prefix, k, v)
		if type(v) == "table" then
			dump(v, prefix .. "." .. k)
		end
	end
end

if mode == "child" then

	local datasheet = require "skynet.datasheet"

	skynet.start(function()
		local t = datasheet.query("foobar")
		dump(t, "[CHILD]")

		skynet.sleep(100)
		skynet.exit()
	end)

else

local builder = require "skynet.datasheet.builder"
local datasheet = require "skynet.datasheet"

skynet.start(function()
	builder.new("foobar", {a = 1, b = 2 , c = {3} })
	skynet.newservice(SERVICE_NAME, "child")
	local t = datasheet.query "foobar"
	local c = t.c
	dump(t, "[1]")
	builder.update("foobar", { b = 4, c = { 5 } })
	print("sleep")
	skynet.sleep(100)
	dump(t, "[2]")
	dump(c, "[2.c]")
	builder.update("foobar", { a = 6, c = 7, d = 8 })
	print("sleep")
	skynet.sleep(100)
	dump(t, "[3]")
end)

end