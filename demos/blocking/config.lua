root = "../../"

--lualoader = root .. "lualib/loader.lua"             -- 用哪一段lua代码加载lua服务
--luaservice = root.."service/?.lua;".."./?.lua;"     -- lua服务代码所在的位置
--lua_path = root.."lualib/?.lua;"..root.."lualib/?/init.lua"
--lua_cpath = root .. "luaclib/?.so"
--snax = root.."01-helloworld/?.lua;"     -- 用snax框架编写的服务的查找路径

luaservice = root.."service/?.lua;"..root.."test/?.lua;".."./?.lua;"..root.."test/?/init.lua"
lualoader = root .. "lualib/loader.lua"
lua_path = root.."lualib/?.lua;"..root.."lualib/?/init.lua"
lua_cpath = root .. "luaclib/?.so"
snax = root.."./?.lua;"..root.."test/?.lua"

thread = 4      -- 启动多少个工作线程, 默认8个
logger = nil    -- skynet内建的skynet_error这个C API将信息输出到什么文件中, 默认为nil
logpath = "."
harbor = 0                      -- 集群网络配置, 0:工作在单节点模式下, master和address以及standalone都不必设置
--address = "127.0.0.1:2526"    -- 集群网络配置, 当前skynet节点的地址和端口
--standalone = "0.0.0.0:2013"   -- 集群网络配置
--master = "127.0.0.1:2013"     -- 集群网络配置
start = "main"	                -- 入口,默认为main
bootstrap = "snlua bootstrap"	-- skynet启动的第一个服务以及其启动参数。默认配置为 snlua bootstrap

-- snax_interface_g = "snax_g"
cpath = root.."cservice/?.so"   -- C编写的服务模块的位置, 默认为./cservice/?.so
-- daemon = "./skynet.pid"      -- 后台模式：可以以后台模式启动skynet（注意，同时请配置logger项输出log）
