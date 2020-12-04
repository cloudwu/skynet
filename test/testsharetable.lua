local skynet = require "skynet"
local sharetable = require "skynet.sharetable"

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
end)
