local skynet = require "skynet"
local string = string
local table = table
local tonumber = tonumber
local redis_server = ...
local fd
local write_fd
local readline_fd
local read_fd

local function init_fd(fdstr)
	fd = fdstr
	write_fd = "WRITE "..fd.." "
	readline_fd = "READLINE ".. fd .." \r\n"
	read_fd = "READ " .. fd .. " "
end

skynet.dispatch(function(message, ...)
	skynet.send(".connection", write_fd .. message)
	local result = skynet.call(".connection", readline_fd)
	local firstchar = string.byte(result)
	if firstchar == 42 then	-- '*'
		local n = tonumber(string.sub(result,2))
		if n < 1 then
			skynet.ret(result .. "\r\n")
			return
		end
		local bulk = { result }
		for i = 1,n do
			local line = skynet.call(".connection", readline_fd)
			table.insert(result, line)
			local bytes = tonumber(string.sub(line,2) + 2)
			table.insert(result, bytes)
		end
		table.insert(result,"")
		skynet.ret(table.concat(result,"\r\n"))
	elseif firstchar == 36 then -- '$'
		local bytes = tonumber(string.sub(result,2))
		if bytes < 0 then
			skynet.ret(result .. "\r\n")
			return
		end
		local firstline = skynet.call(".connection", read_fd .. (bytes + 2))
		skynet.ret(result .. "\r\n" .. firstline)
	else
		skynet.ret(result .. "\r\n")
	end
end)

skynet.start(function()
	fd = skynet.call(".connection", "CONNECT " .. redis_server)
	if fd == nil then
		print("Connect to redis server error : ", redis_server)
		skynet.exit()
		return
	end
	init_fd(fd)
end)
