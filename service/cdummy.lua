local skynet = require "skynet"
require "skynet.manager"	-- import skynet.launch, ...

local globalname = {}
local queryname = {}
local harbor = {}
local harbor_service

skynet.register_protocol {
	name = "harbor",
	id = skynet.PTYPE_HARBOR,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
}

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
}

local function response_name(name)
	local address = globalname[name]
	if queryname[name] then
		local tmp = queryname[name]
		queryname[name] = nil
		for _,resp in ipairs(tmp) do
			resp(true, address)
		end
	end
end

function harbor.REGISTER(name, handle)
	assert(globalname[name] == nil)
	globalname[name] = handle
	response_name(name)
	skynet.redirect(harbor_service, handle, "harbor", 0, "N " .. name)
end

function harbor.QUERYNAME(name)
	if name:byte() == 46 then	-- "." , local name
		skynet.ret(skynet.pack(skynet.localname(name)))
		return
	end
	local result = globalname[name]
	if result then
		skynet.ret(skynet.pack(result))
		return
	end
	local queue = queryname[name]
	if queue == nil then
		queue = { skynet.response() }
		queryname[name] = queue
	else
		table.insert(queue, skynet.response())
	end
end

function harbor.LINK(id)
	skynet.ret()
end

function harbor.CONNECT(id)
	skynet.error("Can't connect to other harbor in single node mode")
end

skynet.start(function()
	local harbor_id = tonumber(skynet.getenv "harbor")
	assert(harbor_id == 0)

	skynet.dispatch("lua", function (session,source,command,...)
		local f = assert(harbor[command])
		f(...)
	end)
	skynet.dispatch("text", function(session,source,command)
		-- ignore all the command
	end)

	harbor_service = assert(skynet.launch("harbor", harbor_id, skynet.self()))
end)
