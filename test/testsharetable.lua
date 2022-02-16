local skynet = require "skynet"
local sharetable = require "skynet.sharetable"

local function queryall_test()
	sharetable.loadtable("test_one", {["message"] = "hello one", x = 1, 1})
	sharetable.loadtable("test_two", {["message"] = "hello two", x = 2, 2})
	sharetable.loadtable("test_three", {["message"] = "hello three", x = 3, 3})
	local list = sharetable.queryall({"test_one", "test_two"})
	for filename, tbl in pairs(list) do
		for k, v in pairs(tbl) do
			print(filename, k, v)
		end
	end

	print("test queryall default")
	local defaultlist = sharetable.queryall()
	for filename, tbl in pairs(defaultlist) do
		for k, v in pairs(tbl) do
			print(filename, k, v)
		end
	end
end

skynet.start(function()
	-- You can also use sharetable.loadfile / sharetable.loadstring
	sharetable.loadtable ("test", { x=1,y={ 'hello world' },['hello world'] = true })
	local t = sharetable.query("test")
	for k,v in pairs(t) do
		print(k,v)
	end
	sharetable.loadstring ("test", "return { ... }", 1,2,3)
	local t = sharetable.query("test")
	for k,v in pairs(t) do
		print(k,v)
	end

	queryall_test()
end)
