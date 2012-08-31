local skynet = require "skynet"

local obj = { id = 0 }

function obj.hello()
	return "Hello"
end

function obj:inc()
	self.id = self.id + 1
	return self.id
end

skynet.start(function()
	skynet.remote_create(obj)
	local root = skynet.remote_root()
	print(root.echo("hello"))
	root.register("test",obj)
	local qobj = root.query "test"
	print(qobj.hello())
	print(qobj:inc())
	skynet.exit()
end)
