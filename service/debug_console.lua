local skynet = require "skynet"
local socket = require "socket"

local port = tonumber(...)

local function format_table(t)
	local index = {}
	for k in pairs(t) do
		table.insert(index, k)
	end
	table.sort(index)
	local result = {}
	for _,v in ipairs(index) do
		table.insert(result, string.format("%s:%s",v,tostring(t[v])))
	end
	return table.concat(result,"\t")
end

local function dump_line(print, key, value)
	if type(value) == "table" then
		print(key, format_table(value))
	else
		print(key,tostring(value))
	end
end

local function dump_list(print, list)
	local index = {}
	for k in pairs(list) do
		table.insert(index, k)
	end
	table.sort(index)
	for _,v in ipairs(index) do
		dump_line(print, v, list[v])
	end
end

local function docmd(cmdline, print)
	local split = {}
	for i in string.gmatch(cmdline, "%S+") do
		table.insert(split,i)
	end
	local ok, list = pcall(skynet.call,".launcher","lua", table.unpack(split))
	if ok then
		if list then
			dump_list(print, list)
		end
	else
		print("Error:", list)
	end
end

local function console_main_loop(stdin, print)
	socket.lock(stdin)
	print("Welcome to skynet console")
	while true do
		local cmdline = socket.readline(stdin, "\n")
		if not cmdline then
			break
		end
		if cmdline ~= "" then
			docmd(cmdline, print)
		end
	end
	socket.unlock(stdin)
end

skynet.start(function()
	local listen_socket = socket.listen ("127.0.0.1", port)
	print("Start debug console at 127.0.0.1",port)
	socket.start(listen_socket , function(id, addr)
		local function print(...)
			local t = { ... }
			socket.write(id, table.concat(t,"\t"))
			socket.write(id, "\n")
		end
		socket.start(id)
		skynet.fork(console_main_loop, id , print)
	end)
end)
