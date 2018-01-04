local skynet = require "skynet"
local harbor = require "skynet.harbor"
require "skynet.manager"	-- import skynet.launch, ...
local memory = require "skynet.memory"


-- 执行了 skynet.start 这个接口，这也是所有 lua服务 的标准启动入口, 服务启动完成后，就会调用这个接口
-- 传入的参数就是一个function（方法），而且这个方法就是此 lua服务 的在lua层的回调接口，本服务的消息都在此回调方法中执行
-- lualib/skynet.lua   skynet.start(start_func)
skynet.start(function()
	local sharestring = tonumber(skynet.getenv "sharestring" or 4096)
	memory.ssexpand(sharestring)

	-- 根据 standalone 配置项判断你启动的是一个 master 节点还是 slave 节点
	local standalone = skynet.getenv "standalone"

	local launcher = assert(skynet.launch("snlua","launcher"))
	skynet.name(".launcher", launcher)


    -- 通过harbor是否配置为 0 来判断你是否启动的是一个单节点 skynet 网络
	local harbor_id = tonumber(skynet.getenv "harbor" or 0)
	if harbor_id == 0 then -- 单节点模式 skynet网络(不需要通过内置的 harbor 机制做节点间通讯的)
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")

		-- 为了兼容（因为你还是有可能注册全局名字），需要启动一个叫做 cdummy 的服务，它负责拦截对外广播的全局名字变更
		local ok, slave = pcall(skynet.newservice, "cdummy")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)

	else -- 多节点模式 skynet网络
		if standalone then  -- master节点(需要启动 cmaster 服务作节点调度用)
			if not pcall(skynet.newservice,"cmaster") then
				skynet.abort()
			end
		end
		-- 每个节点（包括 master 节点自己）都需要启动 cslave 服务，用于节点间的消息转发，以及同步全局名字
		local ok, slave = pcall(skynet.newservice, "cslave")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)
	end

    -- 根据 standalone 配置项判断你启动的是一个 master 节点还是 slave 节点
	if standalone then  -- 在 master 节点上，还需要启动 DataCenter 服务
		local datacenter = skynet.newservice "datacenterd"
		skynet.name("DATACENTER", datacenter)
	end

	-- 启动用于 UniqueService 管理的 service_mgr
	skynet.newservice "service_mgr"

	-- 根据Conifg中配置的start项启动下一个lua服务，假如无此项配置，则启动main.lua
	pcall(skynet.newservice,skynet.getenv "start" or "main")

	-- 退出bootstrap服务
	skynet.exit()
end)
