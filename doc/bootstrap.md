1. config
2. skynet-src/skynet_start.c
```C
   static void
   bootstrap(struct skynet_context * logger, const char * cmdline) {
   	int sz = strlen(cmdline);
   	char name[sz+1];
   	char args[sz+1];
   	sscanf(cmdline, "%s %s", name, args);
   	struct skynet_context *ctx = skynet_context_new(name, args);
   	if (ctx == NULL) {
   		skynet_error(NULL, "Bootstrap error : %s\n", cmdline);
   		skynet_context_dispatchall(logger);
   		exit(1);
   	}
   }
   
   
    void 
    skynet_start(struct skynet_config * config) {
        //启动的服务是bootstrap，但先要加载snlua模块，所有的lua服务都属于snlua模块的实例。
        //加载引导模块 默认 config->bootstrap = "snlua bootstrap",并使用snlua服务启动bootstrap.lua脚本
        //不使用snlua也可以直接启动其他服务的动态库
    	bootstrap(ctx, config->bootstrap);
    }

```
3. skynet-src/skynet_server.c
```C
    struct skynet_context * 
    skynet_context_new(const char * name, const char *param) {
        struct skynet_module * mod = skynet_module_query(name); 	//1. 根据名字查询模块 根据名字查询模块snlua等
    
        void *inst = skynet_module_instance_create(mod);			//2. 根据模块创建实例 snlua_create()创建lua VM
    
        struct skynet_context * ctx = skynet_malloc(sizeof(*ctx));	//3. 为skynet上下文分配内存
        
        ctx->handle = skynet_handle_register(ctx);					//4. 注册分配句柄
        
        struct message_queue * queue = ctx->queue = skynet_mq_create(ctx->handle);	// 5. 创建队列消息
    
        int r = skynet_module_instance_init(mod, inst, ctx, param); //执行snlua_init()完成服务的创建 // 6. 调用模块实例的初始化函数
    
        if (r == 0) {
            skynet_globalmq_push(queue);							//将队列push到全局队列中
        } 
        else{
        
        }
    }
```

4. skynet-src/skynet_module.c
```C
    skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
        return m->init(inst, ctx, parm);// service_logger.c logger_init()
    }
```


5. service/service_snlua.c
```C
    int
    snlua_init(struct snlua *l, struct skynet_context *ctx, const char * args) {
        int sz = strlen(args);
        char * tmp = skynet_malloc(sz);
        memcpy(tmp, args, sz);
        skynet_callback(ctx, l , launch_cb);//1. 给当前服务实例注册绑定了launch_cb，有消息传入时会调用回调函数并处理
        const char * self = skynet_command(ctx, "REG", NULL);
        uint32_t handle_id = strtoul(self+1, NULL, 16);
        // it must be first message
    
        // 向自己发送了一条消息，并附带了一个参数，这个参数就是bootstrap。
        // 当把消息队列加入到全局队列后，收到的第一条消息就是这条消息。
        // 收到第一条消息后，调用到callback函数，也就是service_snlua.c里的_launch方法
        skynet_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY,0, tmp, sz);//2. 给本服务发送一条消息，内容就是之前传入的参数args = tmp: bootstrap
        return 0;
    }
    
    
    
    
    static int
    init_cb(struct snlua *l, struct skynet_context *ctx, const char * args, size_t sz) {
    	
    	//loader的作用是以cml参数为名去各项代码目录查找lua文件，找到后loadfile并执行(等效于dofile)
    	const char * loader = optstring(ctx, "lualoader", "./lualib/loader.lua");//加载执行了lualib/loader.lua文件

    	return 0;
    }
    
    
```


6.service/bootstarp.lua
```lua
    local skynet = require "skynet"
    local harbor = require "skynet.harbor"
    require "skynet.manager"	-- import skynet.launch, ...
    local memory = require "skynet.memory"
    
    skynet.start(function()
        ---
    end)
```