local core = require "skynet.reload.core"
local skynet = require "skynet"

local function reload(...)
	local args = SERVICE_NAME .. " " .. table.concat({...}, " ")
	print(args)
end

return reload
