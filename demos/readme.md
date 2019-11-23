# 编译
## jemalloc
默认使用jemalloc内存分配器,可以减少内存碎片
在内存紧张的设置上,可以修改
```
platform.mk:
linux : SKYNET_DEFINES :=-DNOUSE_JEMALLOC　#禁用jemalloc
```
禁用后,skynet编译后大小变小,使用libc自带的内存分配器

## tls
```
Makefile:
TLS_MODULE=ltls #使用tls/https
```
可以配置是否使用tls. 如果没有https请求,可以禁用.

## 最大socket数
```
skynet-src/socket_server.c:
#define MAX_SOCKET_P 6
```
最大支持 ```2＾MAX_SOCKET_P``` 个socket. 配置越大，启动内存越多

# 架构
https://github.com/cloudwu/skynet/wiki/GettingStarted
## 特性
    * 多线程
    * 异步
    * 多任务
## 网络
    * TCP 
    * UDP    
    * websocket
## service
* 唯一ID
* 由其他service启动
    * newservice: 普通服务
    * uniqueservice: 唯一服务
* 三个运行阶段
    * 加载
    * 初始化: 
        * newservice/uniqueservice等待初始化函数结束
        * 注册消息处理函数
    * 工作: 消息处理
* 多业务流
    * fork
    * timeout
    * coroutine    
* 轮流获得执行权(串行)
* 主动让出控制权(阻塞操作)
    * 内部状态可能已经改变
* 服务之间互相独立，通过消息交互
* service:线程 = n:m
## 时间精度: 0.01s
## 消息
* 组成:
```
(消息类型,session,发起服务地址,接收服务地址,消息C指针,消息长度)
type: 消息类型, 处理多类消息

```
* 消息类型
    * Lua消息（常用）
    * 回应消息
    * 网络消息
    * 调试消息
    * 文本消息
    * 错误
* 外部服务
    * sqlite
    * mysql 
    * redis 
    * web

# 配置
https://github.com/cloudwu/skynet/wiki/Config

```lua
root = "../../"
lualoader = root .. "lualib/loader.lua"   -- 用哪一段lua代码加载lua服务
luaservice = root.."service/?.lua;".."./?.lua;"  -- lua服务代码所在的位置, 包括系统服务和自定义服务
lua_path = root.."lualib/?.lua;"..root.."lualib/?/init.lua"
lua_cpath = root .. "luaclib/?.so"
snax = root.."01-helloworld/?.lua;" -- 用snax框架编写的服务的查找路径

thread = 8      -- 启动多少个工作线程, 默认8个
logger = nil    -- skynet内建的skynet_error这个C API将信息输出到什么文件中, 默认为nil
logpath = "."   -- 日志目录
harbor = 0      -- 集群网络配置, 0:工作在单节点模式下, master和address以及standalone都不必设置
--address = "127.0.0.1:2526"    -- 集群网络配置, 当前skynet节点的地址和端口
--standalone = "0.0.0.0:2013"   -- 集群网络配置
--master = "127.0.0.1:2013"     -- 集群网络配置
start = "main"	                -- 入口,默认为main
bootstrap = "snlua bootstrap"	-- skynet启动的第一个服务以及其启动参数。默认配置为 snlua bootstrap

-- snax_interface_g = "snax_g"
cpath = root.."cservice/?.so"   -- C编写的服务模块的位置, 默认为./cservice/?.so
-- daemon = "./skynet.pid"      -- 后台模式：可以以后台模式启动skynet（注意，同时请配置logger项输出log）
```
# 实例
API: https://github.com/cloudwu/skynet/wiki/APIList
## service
### newservice
counter.lua
```lua
local counter = 0
skynet.start(function()
    skynet.dispatch("lua", function(session, address, cmd, ...)
        if cmd == "active" then
            counter = counter + 1
            return
        end

        if cmd == "current_count" then
            skynet.retpack(counter)
            return
        end

        error(string.format("Unknown command %s", tostring(cmd)))
    end)
end)
```
main.lua
```lua
skynet.start(function()
	-- newservice(name, ...) 启动一个名为name的新服务. 等待到初始化函数结束才会返回
	local counter1 = skynet.newservice("counter", 1)
	local counter2 = skynet.newservice("counter", 2)		-- 新的服务

	print("counter1 address:", counter1)
	print("counter2 address:", counter2)

	-- send(addr, type, ...) 用type类型向addr发送一个消息，不等待回应
	skynet.send(counter1, "lua", "active")

	-- call(addr, type, ...) 用type类型发送一个消息到addr，并等待对方的回应
	local res = skynet.call(counter1, "lua", "current_count")
	print("call counter1: ", res)

    -- 调用错误的cmd
	local res, err = pcall(skynet.call, counter1, "lua", "not exists")
	print("=========== ", res, err)

	skynet.exit()	-- 退出当前服务
end)
```
### uniqueservice
https://github.com/cloudwu/skynet/wiki/UniqueService

mydb.lua 
```lua
local db = {}
skynet.start(function()
    skynet.dispatch("lua", function(session, address, cmd, key, value)
        if cmd == "set" then
            local last = db[key]
            db[key] = value
            return skynet.retpack(last)
        end

        if cmd == "get" then
            skynet.retpack(db[key])
            return
        end

        error(string.format("Unknown command %s", tostring(cmd)))
    end)

    skynet.register("MyDB")     -- register(name) 给当前服务起一个字符串名
end)
```
main.lua
```lua
skynet.start(function()
    -- uniqueservice(name, ...) 启动一个唯一服务，如果服务该服务已经启动，则返回已启动的服务地址
    skynet.uniqueservice("mydb")	
    skynet.uniqueservice("mydb")	-- 第二次创建,使用第一次创建的实例  

    local res = skynet.call("MyDB", "lua", "set", "name", "Lily")
    print("last name", res) 

    local res = skynet.call("MyDB", "lua", "set", "name", "Tommy")
    print("last name", res) 

    local res = skynet.call("MyDB", "lua", "set", "hoby", {"singing", "swimming"})
    print("last hoby", res) 

    local res = skynet.call("MyDB", "lua", "get", "hoby")
    print("get hoby", table.concat(res, ","))   

    skynet.exit()	-- 退出当前服务
end)
```
## 定时器
```lua
--　自定义interval函数
skynet.interval = function(interval, cb)
	local f
	f = function()
		cb()
		skynet.timeout(interval, f)
	end
	skynet.timeout(interval, f)
end

-- 时间精度: 0.01s
skynet.start(function()
	skynet.timeout(200, function()
		print("timeout: now: ", skynet.now())
	end)

	--　定时执行１．　fork(func, ...) 启动一个新的任务去执行函数 func　
	skynet.fork(function()
		skynet.sleep(200)	-- sleep(time) 让当前的任务等待 time*0.01s
		print("fork: now: ", skynet.now())
	end)

	-- 定时执行２
	skynet.interval(300, function()
		print("interval", skynet.now())
	end)
end)
```
## TCP
https://github.com/cloudwu/skynet/wiki/Socket

服务器端

server.lua 
```lua 
local function accept(id)
    socket.start(id)
    socket.write(id, "Hello Skynet\n")
    skynet.newservice("agent", id)
    -- 注意：在新服务调用socket.start前，客户端如果发送数据，将会丢失
    -- 清除 socket id 在本服务内的数据结构，但并不关闭这个 socket
    socket.abandon(id)
end

skynet.start(function()
    local id = socket.listen("127.0.0.1", 8001)
    print("Listen socket :", "127.0.0.1", 8001)

    -- socket.start(id , accept) accept是一个函数
    -- 每当一个监听的id对应的socket上有连接接入的时候，都会调用accept函数
    socket.start(id , function(id, addr)
        print("connect from " .. addr .. " " .. id)
        -- you have choices :
        -- 1. skynet.newservice("testsocket", "agent", id)
        -- 2. skynet.fork(echo, id)
        -- 3. accept(id)
        accept(id)
    end)
end)
```
agent.lua 
```lua 
local id = ...
local function echo(id)
    -- 调用 socket.start(id) 之后，才可以收到这个 socket 上的数据
    socket.start(id)

    while true do
        local str = socket.read(id)
        if str then
            socket.write(id, str)
        else
            socket.close(id)
            print("close id", id)
            return
        end
    end
end

skynet.start(function()
    local id = tonumber(id)
    skynet.fork(function()
        echo(id)
        skynet.exit()
    end)
end)
```
客户端

main.lua 
```lua 
skynet.start(function()
	skynet.newservice("server")
	skynet.fork(function()
		local id, err = socket.open("127.0.0.1", 8001)
		assert(not err)

		skynet.sleep(100)

		for i = 1, 10 do
			local s = socket.read(id)
			print("read", s)
			socket.write(id, tostring(os.time()))
			skynet.sleep(50)
		end

		socket.close(id)
		skynet.exit()
	end)
end)
```
## UDP
https://github.com/cloudwu/skynet/wiki/Socket

```lua
local function server()
	local host
	local on_packet = function(str, from)
		print("server recv", str, socket.udp_address(from))
		socket.sendto(host, from, "OK " .. str)
	end

	-- bind an address
	host = socket.udp(on_packet, "127.0.0.1", 8765)
end

local function client()
	local on_packet = function(str, from)
		print("client recv", str, socket.udp_address(from))
	end

	local cli = socket.udp(on_packet)
	socket.udp_connect(cli, "127.0.0.1", 8765)

	for i=1, 20 do
		-- write to the address by udp_connect binding
		socket.write(cli, "hello " .. i)
		skynet.sleep(50)
	end
end

skynet.start(function()
	skynet.fork(server)
	skynet.fork(client)
end)
```
## http
https://github.com/cloudwu/skynet/wiki/Http

http服务器

websrv.lua
```lua
skynet.start(function()
	--　启动agent处理收到的请求，可以改成多个
	local agent = skynet.newservice("agent")

	--　监听socket
	local id = socket.listen("0.0.0.0", 8001)
	skynet.error("listen ok :8001")

	-- 开始接收数据
	socket.start(id , function(cid, addr)
		-- 转发给agent处理
		skynet.send(agent, "lua", cid)
	end)
end)
```
agent.lua
```lua
local handler = {}
handler["/timestamp"] = function(method, header, query, body)
    local params = urllib.parse_query(query)
    local timeout = tonumber(params.timeout)
    if timeout then
        skynet.sleep(100*timeout)
    end
    return 200, tostring(os.time())
end

handler["/post"] = function(method, header, query, body)
    ...
end

handler["/echo"] = function(method, header, query, body)
    ...
end

skynet.start(function()
    skynet.dispatch("lua", function(session, address, id)
        socket.start(id)  --　开始读取数据　

        local read = sockethelper.readfunc(id)
        local write = sockethelper.writefunc(id)

        -- 读取请求
        local code, url, method, header, body = httpd.read_request(read, nil)
        if not code then    --　错误处理
            if url == sockethelper.socket_error then
                skynet.error("socket closed")
            else
                skynet.error(url)
            end
            return socket.close(id) --　关闭连接　
        end

        if code ~= 200 then
            return httpd.write_response(write, 400, "invalid request")
        end

        local path, query = urllib.parse(url)
        local f = handler[path]
        if not f then
            return httpd.write_response(write, 400, "invalid request")
        end

        local code, data = f(method, header, query, body) --　处理请求
        return httpd.write_response(write, code, data)    --　回复结果
    end)
end)
```
http客户端

main.lua

```lua 
local host = "http://127.0.0.1:8001"

-- 注意：用pcall防止coroutine异常退出
local function http_timeout()
    local respheader = {}
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(ok)

    local respheader = {}
    httpc.timeout = 10	-- set timeout 0.1s
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(not ok)

    local respheader = {}
    httpc.timeout = 1000	-- set timeout 10s
    local ok, status, body = pcall(httpc.get, host, "/timestamp?timeout=3", respheader)
    assert(ok)
end

-- httpc.get(host, url, recvheader, header)
local function http_get()
    --　自定义header
    local respheader = {}
    local header = { host = "test.com" }
    local ok, status, body = pcall(httpc.get, host, "/echo", respheader, header)
    assert(ok)
    print(status, body)
end

-- httpc.post(host, url, form, recvheader)
local function http_post()
    --　自定义header
    local respheader = {}
    local form = {
        timestamp = skynet.now(),
        username = "xxx",
    }
    local ok, status, body = pcall(httpc.post, host, "/post", form, respheader)
    assert(ok)
    print(status, body)
end

local function escape(s)
    return (string.gsub(s, "([^A-Za-z0-9_])", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

function httpc.post_json(host, url, form)
    local header = {
        ["content-type"] = "application/json"
    }

    local body = {}
    for k,v in pairs(form) do
        table.insert(body, string.format("%s=%s", escape(k), escape(v)))
    end

    return httpc.request("POST", host, url, {}, header, table.concat(body , "&"))
end

-- httpc.request(method, host, url, recvheader, header, content)
local function http_post_json()
    local form = {
        timestamp = skynet.now(),
        username = "xxx",
    }

    local ok, status, body = pcall(httpc.post_json, host, "/post", form)
    assert(ok)
    print(status, body)
end

function https_get()
    if not pcall(require,"ltls.c") then
    	print "No ltls module, https is not supported"
        return
    end

    local ok, status, body = pcall(httpc.get, "https://www.baidu.com", "/", respheader)
    assert(ok)
    print(status, #body)
end
```
## multicast组播
```lua
-- producert
local function producer()
	skynet.start(function()
		local channel = mc.new()	-- 创建一个频道，成功创建后，.channel 是这个频道的 id

		for i = 1, 10 do
			local sub = skynet.newservice(SERVICE_NAME, "sub")  -- 启动新服务
			skynet.call(sub, "lua", "init", channel.channel)    --　新服务订阅广播
		end

		print(skynet.address(skynet.self()), "===>", channel)
		channel:publish("Hello World " .. os.time())            --　发送广播

		channel:delete()	-- 删除频道
	end)
end
```
```lua
-- consumer 
local function consumer()
	local dispatchs_message = function (channel, source, ...)
		print(string.format("%s <=== %s %s", skynet.address(skynet.self()), skynet.address(source), channel))
		print(channel, source, ...)
	end

	skynet.start(function()
		skynet.dispatch("lua", function (_,_, cmd, channel)
			assert(cmd == "init")
			local c = mc.new {
				channel = channel,              -- 绑定上频道
				dispatch = dispatchs_message,	-- 设置这个频道的消息处理函数
			}

			print(skynet.address(skynet.self()), "sub", c)

			c:subscribe()		--　订阅

			skynet.ret(skynet.pack())
		end)
	end)
end
```
## redis
## mysql
## 重入
## 文件读写