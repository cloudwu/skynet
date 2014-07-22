local skynet = require "skynet"
local socket = require "socket"
local httpd = require "http.httpd"
local sockethelper = require "http.sockethelper"

local mode = ...

if mode == "agent" then

skynet.start(function()
	skynet.dispatch("lua", function (_,_,id)
		socket.start(id)
		local code, url, method, header, body = httpd.read_request(sockethelper.readfunc(id))
		if code then
			if code ~= 200 then
				httpd.write_response(sockethelper.writefunc(id), code)
			else
				httpd.write_response(sockethelper.writefunc(id), code , "Hello world")
			end
		else
			if url == sockethelper.socket_error then
				skynet.error("socket closed")
			else
				skynet.error(url)
			end
		end
		socket.close(id)
	end)
end)

else

skynet.start(function()
	local agent = {}
	for i= 1, 20 do
		agent[i] = skynet.newservice(SERVICE_NAME, "agent")
	end
	local balance = 1
	local id = socket.listen("0.0.0.0", 8001)
	socket.start(id , function(id, addr)
		skynet.error(string.format("%s connected, pass it to agent :%08x", addr, agent[balance]))
		skynet.send(agent[balance], "lua", id)
		balance = balance + 1
		if balance > #agent then
			balance = 1
		end
	end)
end)

end