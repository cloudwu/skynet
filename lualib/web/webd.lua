local skynet = require "skynet"
local socket = require "socket"
local httpd = require "http.httpd"
local sockethelper = require "http.sockethelper"
local urllib = require "http.url"
local table = table
local string = string
local sweb = require "web.sweb"
local mode,web_root= ...
local lpeg =require "lpeg"
local file_caches_name = "file_canches_name"
if mode == "agent" then
local function response(id, ...)
   local ok, err = httpd.write_response(sockethelper.writefunc(id), ...)
   if not ok then
      -- if err == sockethelper.socket_error , that means socket closed.
      skynet.error(string.format("fd = %d, %s", id, err))
   end
end

local function is_static(path)
   local R, S, V, P = lpeg.R, lpeg.S, lpeg.V, lpeg.P
   local C, Ct, Cmt, Cg, Cb, Cc = lpeg.C, lpeg.Ct, lpeg.Cmt, lpeg.Cg, lpeg.Cb, lpeg.Cc
   local prefix = P".gif" +
      P".js" +
      P".html" +
      P".css" +
      P".jpeg"
   local any = (P(1) - P".")^1 
   local uri = P"/" * any * prefix
   if uri:match(path) then
      return true
   else
      return false
   end
end

local function open_file(id,file_path)
   local root = web_root
   local key = root..file_path
   local data 
   local cache = skynet.queryservice(SERVICE_NAME)
   data = skynet.call(cache,"lua","get",key)
   if not data then
      local file = io.open(key, "rb") 
      if not file then
	 return response(id,501,"file not exist!")
      end
      local size = file:seek("end")  
      local header = {}
      header["Content-Length"] = size  
      file:seek("set", 0)   
      data = file:read("*a")
      file:close()  
      skynet.call(cache,"lua","set",key,data)
   end
   response(id,200,data,header)
end

skynet.start(function()
		skynet.dispatch("lua", function (_,_,id)
				   socket.start(id)
				   -- limit request body size to 8192 (you can pass nil to unlimit)
				   local code, url, method, header, body = httpd.read_request(sockethelper.readfunc(id), 8192)
				   if code then
				      if code ~= 200 then
					 response(id, code)
				      else
					 if is_static(url) then
					    open_file(id,url)
					 else
					    response(id,sweb.handle(url, method, header, body,web_root))
					 end
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

elseif mode =="master" then
   skynet.start(function(...)		   
		   skynet.dispatch("lua",function(_,_,cmd,port,config)
				      if not config then config ={} end
				      local thread = config.thread or 100
				      local port = port
				      local web_root = config.web_root or "./"
		   if not port then port = 8001 end
		   local agent = {}
		   for i= 1, thread do
		      agent[i] = skynet.newservice(SERVICE_NAME, "agent",web_root)
		   end
		   local balance = 1
		   local id = socket.listen("0.0.0.0", port)
		   local ss = skynet.uniqueservice(SERVICE_NAME)
		   socket.start(id , function(id, addr)				   
				   skynet.send(agent[balance], "lua", id)
				   balance = balance + 1
				   if balance > #agent then
				      balance = 1
				   end
		   end)
		   skynet.ret(skynet.pack(true))
		   end)
   end)
else
   local file_caches = {}
   skynet.start(function(...)
		   skynet.dispatch("lua", function (_,_, cmd,key,value)
				      if cmd == "get" then
					 local v = file_caches[key]
					 skynet.ret(skynet.pack(v))
				      elseif cmd == "set" then
					 file_caches[key] = value
					 skynet.ret(skynet.pack(true))
				      else
					 print("not support :",cmd)
				      end
		   end)
   end)
end