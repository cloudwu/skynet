local skynet = require "skynet"

local service = {}
local root = {}

function root.register(name, obj)
	service[name] = obj
end

function root.query(name)
	return service[name]
end

function root.echo(hello)
	return hello
end

skynet.remote_create(root,0)

skynet.start(function() end)

