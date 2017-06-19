local skynet = require "skynet"
local codecache = require "skynet.codecache"
local core = require "skynet.core"
local socket = require "socket"
local snax = require "snax"
local memory = require "memory"
local httpd = require "http.httpd"
local sockethelper = require "http.sockethelper"

local port = tonumber(...)
local COMMAND = {}

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
	print("OK")
end

local function split_cmdline(cmdline)
	local split = {}
	for i in string.gmatch(cmdline, "%S+") do
		table.insert(split,i)
	end
	return split
end

local function docmd(cmdline, print, fd)
	local split = split_cmdline(cmdline)
	local command = split[1]
	local cmd = COMMAND[command]
	local ok, list
	if cmd then
		ok, list = pcall(cmd, fd, select(2,table.unpack(split)))
	else
		print("Invalid command, type help for command list")
	end

	if ok then
		if list then
			if type(list) == "string" then
				print(list)
			else
				dump_list(print, list)
			end
		else
			print("OK")
		end
	else
		print("Error:", list)
	end
end

local function console_main_loop(stdin, print)
	print("Welcome to skynet console")
	skynet.error(stdin, "connected")
	pcall(function()
		while true do
			local cmdline = socket.readline(stdin, "\n")
			if not cmdline then
				break
			end
			if cmdline:sub(1,4) == "GET " then
				-- http
				local code, url = httpd.read_request(sockethelper.readfunc(stdin, cmdline.. "\n"), 8192)
				local cmdline = url:sub(2):gsub("/"," ")
				docmd(cmdline, print, stdin)
				break
			end
			if cmdline ~= "" then
				docmd(cmdline, print, stdin)
			end
		end
	end)
	skynet.error(stdin, "disconnected")
	socket.close(stdin)
end

skynet.start(function()
	local listen_socket = socket.listen ("127.0.0.1", port)
	skynet.error("Start debug console at 127.0.0.1 " .. port)
	socket.start(listen_socket , function(id, addr)
		local function print(...)
			local t = { ... }
			for k,v in ipairs(t) do
				t[k] = tostring(v)
			end
			socket.write(id, table.concat(t,"\t"))
			socket.write(id, "\n")
		end
		socket.start(id)
		skynet.fork(console_main_loop, id , print)
	end)
end)

function COMMAND.help()
	return {
		help = "This help message",
		list = "List all the service",
		stat = "Dump all stats",
		info = "info address : get service infomation",
		exit = "exit address : kill a lua service",
		kill = "kill address : kill service",
		mem = "mem : show memory status",
		gc = "gc : force every lua service do garbage collect",
		start = "lanuch a new lua service",
		snax = "lanuch a new snax service",
		clearcache = "clear lua code cache",
		service = "List unique service",
		task = "task address : show service task detail",
		inject = "inject address luascript.lua",
		logon = "logon address",
		logoff = "logoff address",
		log = "launch a new lua service with log",
		debug = "debug address : debug a lua service",
		signal = "signal address sig",
		cmem = "Show C memory info",
		shrtbl = "Show shared short string table info",
		ping = "ping address",
	}
end

function COMMAND.clearcache()
	codecache.clear()
end

function COMMAND.start(fd, ...)
	local ok, addr = pcall(skynet.newservice, ...)
	if ok then
		if addr then
			return { [skynet.address(addr)] = ... }
		else
			return "Exit"
		end
	else
		return "Failed"
	end
end

function COMMAND.log(fd, ...)
	local ok, addr = pcall(skynet.call, ".launcher", "lua", "LOGLAUNCH", "snlua", ...)
	if ok then
		if addr then
			return { [skynet.address(addr)] = ... }
		else
			return "Failed"
		end
	else
		return "Failed"
	end
end

function COMMAND.snax(fd, ...)
	local ok, s = pcall(snax.newservice, ...)
	if ok then
		local addr = s.handle
		return { [skynet.address(addr)] = ... }
	else
		return "Failed"
	end
end

function COMMAND.service()
	return skynet.call("SERVICE", "lua", "LIST")
end

local function adjust_address(address)
	if address:sub(1,1) ~= ":" then
		address = assert(tonumber("0x" .. address), "Need an address") | (skynet.harbor(skynet.self()) << 24)
	end
	return address
end

function COMMAND.list()
	return skynet.call(".launcher", "lua", "LIST")
end

function COMMAND.stat()
	return skynet.call(".launcher", "lua", "STAT")
end

function COMMAND.mem()
	return skynet.call(".launcher", "lua", "MEM")
end

function COMMAND.kill(fd, address)
	return skynet.call(".launcher", "lua", "KILL", address)
end

function COMMAND.gc()
	return skynet.call(".launcher", "lua", "GC")
end

function COMMAND.exit(fd, address)
	skynet.send(adjust_address(address), "debug", "EXIT")
end

function COMMAND.inject(fd, address, filename)
	address = adjust_address(address)
	local f = io.open(filename, "rb")
	if not f then
		return "Can't open " .. filename
	end
	local source = f:read "*a"
	f:close()
	return skynet.call(address, "debug", "RUN", source, filename)
end

function COMMAND.task(fd, address)
	address = adjust_address(address)
	return skynet.call(address,"debug","TASK")
end

function COMMAND.info(fd, address, ...)
	address = adjust_address(address)
	return skynet.call(address,"debug","INFO", ...)
end

function COMMAND.debug(fd, address)
	address = adjust_address(address)
	local agent = skynet.newservice "debug_agent"
	local stop
	skynet.fork(function()
		repeat
			local cmdline = socket.readline(fd, "\n")
			cmdline = cmdline and cmdline:gsub("(.*)\r$", "%1")
			if not cmdline then
				skynet.send(agent, "lua", "cmd", "cont")
				break
			end
			skynet.send(agent, "lua", "cmd", cmdline)
		until stop or cmdline == "cont"
	end)
	skynet.call(agent, "lua", "start", address, fd)
	stop = true
end

function COMMAND.logon(fd, address)
	address = adjust_address(address)
	core.command("LOGON", skynet.address(address))
end

function COMMAND.logoff(fd, address)
	address = adjust_address(address)
	core.command("LOGOFF", skynet.address(address))
end

function COMMAND.signal(fd, address, sig)
	address = skynet.address(adjust_address(address))
	if sig then
		core.command("SIGNAL", string.format("%s %d",address,sig))
	else
		core.command("SIGNAL", address)
	end
end

function COMMAND.cmem()
	local info = memory.info()
	local tmp = {}
	for k,v in pairs(info) do
		tmp[skynet.address(k)] = v
	end
	return tmp
end

function COMMAND.shrtbl()
	local n, total, longest, space = memory.ssinfo()
	return { n = n, total = total, longest = longest, space = space }
end

function COMMAND.ping(fd, address)
	address = adjust_address(address)
	local ti = skynet.now()
	skynet.call(address, "debug", "PING")
	ti = skynet.now() - ti
	return tostring(ti)
end
