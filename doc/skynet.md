1. config
- start = "main"	-- main script

2. examples/main.lua
```lua
    local skynet = require "skynet"
    local sprotoloader = require "sprotoloader"
    
    local max_client = 64
    
    skynet.start(function()
        skynet.error("Server start")
        skynet.uniqueservice("protoloader")
        if not skynet.getenv "daemon" then
            local console = skynet.newservice("console")
        end
        skynet.newservice("debug_console",8000)
        skynet.newservice("simpledb")
        local watchdog = skynet.newservice("watchdog")
        skynet.call(watchdog, "lua", "start", {
            port = 8888,
            maxclient = max_client,
            nodelay = true,
        })
        skynet.error("Watchdog listen on", 8888)
        skynet.exit()
    end)
```

3. lualib/skynet.lua
```lua
    local c = require "skynet.core"
    local profile = require "skynet.profile"
    
```

4. lualib-src/lua-skynet.c
```C 
    LUAMOD_API int
    luaopen_skynet_core(lua_State *L) {
    	luaL_checkversion(L);
    
    	luaL_Reg l[] = {
    		{ "send" , lsend },
    		{ "genid", lgenid },
    		{ "redirect", lredirect },
    		{ "command" , lcommand },
    		{ "intcommand", lintcommand },
    		{ "error", lerror },
    		{ "tostring", ltostring },
    		{ "harbor", lharbor },
    		{ "pack", luaseri_pack },
    		{ "unpack", luaseri_unpack },
    		{ "packstring", lpackstring },
    		{ "trash" , ltrash },
    		{ "callback", lcallback },
    		{ "now", lnow },
    		{ NULL, NULL },
    	};
    
    	luaL_newlibtable(L, l);
    
    	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
    	struct skynet_context *ctx = lua_touserdata(L,-1);
    	if (ctx == NULL) {
    		return luaL_error(L, "Init skynet context first");
    	}
    
    	luaL_setfuncs(L,l,1);
    
    	return 1;
    }
```

5. lualib-src/lua-profile.c
```C
    LUAMOD_API int
    luaopen_skynet_profile(lua_State *L) {
        luaL_checkversion(L);
        luaL_Reg l[] = {
            { "start", lstart },
            { "stop", lstop },
            { "resume", lresume },
            { "yield", lyield },
            { "resume_co", lresume_co },
            { "yield_co", lyield_co },
            { NULL, NULL },
        };
        luaL_newlibtable(L,l);
        lua_newtable(L);	// table thread->start time
        lua_newtable(L);	// table thread->total time
    
        lua_newtable(L);	// weak table
        lua_pushliteral(L, "kv");
        lua_setfield(L, -2, "__mode");
    
        lua_pushvalue(L, -1);
        lua_setmetatable(L, -3); 
        lua_setmetatable(L, -3);
    
        lua_pushnil(L);	// cfunction (coroutine.resume or coroutine.yield)
        luaL_setfuncs(L,l,3);
    
        int libtable = lua_gettop(L);
    
        lua_getglobal(L, "coroutine");
        lua_getfield(L, -1, "resume");
    
        lua_CFunction co_resume = lua_tocfunction(L, -1);
        if (co_resume == NULL)
            return luaL_error(L, "Can't get coroutine.resume");
        lua_pop(L,1);
    
        lua_getfield(L, libtable, "resume");
        lua_pushcfunction(L, co_resume);
        lua_setupvalue(L, -2, 3);
        lua_pop(L,1);
    
        lua_getfield(L, libtable, "resume_co");
        lua_pushcfunction(L, co_resume);
        lua_setupvalue(L, -2, 3);
        lua_pop(L,1);
    
        lua_getfield(L, -1, "yield");
    
        lua_CFunction co_yield = lua_tocfunction(L, -1);
        if (co_yield == NULL)
            return luaL_error(L, "Can't get coroutine.yield");
        lua_pop(L,1);
    
        lua_getfield(L, libtable, "yield");
        lua_pushcfunction(L, co_yield);
        lua_setupvalue(L, -2, 3);
        lua_pop(L,1);
    
        lua_getfield(L, libtable, "yield_co");
        lua_pushcfunction(L, co_yield);
        lua_setupvalue(L, -2, 3);
        lua_pop(L,1);
    
        lua_settop(L, libtable);
    
        return 1;
    }
```