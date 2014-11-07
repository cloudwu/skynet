local skynet = require "skynet"

return function (name , G, loader,web_root)
   loader = loader or loadfile
   local mainfunc
   
   local function func_id(id, group)
      local tmp = {}
      local function count( _, name, func)
	 if type(name) ~= "string" then
	    error (string.format("%s method only support string", group))
	 end
	 if type(func) ~= "function" then
	    error (string.format("%s.%s must be function"), group, name)
	 end
	 if tmp[name] then
	    error (string.format("%s.%s duplicate definition", group, name))
	 end
	 tmp[name] = true
	 table.insert(id, { #id + 1, group, name, func} )
      end
      return setmetatable({}, { __newindex = count })
   end

   do
      assert(getmetatable(G) == nil)
      assert(G.init == nil)
      assert(G.exit == nil)
      
      assert(G.accept == nil)
      assert(G.response == nil)
   end
   
   local temp_global = {}
   local env = setmetatable({} , { __index = temp_global })
   local func = {}
  
 
--   local system = { "init", "exit", "hotfix" }  todo
   local system = {}
   
   do
      for k, v in ipairs(system) do
	 system[v] = k
	 func[k] = { k , "system", v }
      end
   end
   
   env.rep = func_id(func, "rep")

   local function init_system(t, name, f)
      local index = system[name]
      if index then
	 if type(f) ~= "function" then
	    error (string.format("%s must be a function", name))
	 end
	 func[index][4] = f
      else
	 temp_global[name] = f
      end
   end
   
   setmetatable(G,	{ __index = env , __newindex = init_system })
--   setmetatable(G,	{ __index = env})
   
   local pattern

   do
      local root = web_root --skynet.getenv "web_root"      
      local errlist = {}
      
      --local filename = string.gsub(pat, "?", name)
      filename = root .. name ..".lua"
      local f , err = loader(filename, "bt", G)
      if f then
	 pattern = pat
	 mainfunc = f
      else
	 table.insert(errlist, err)
	 return false,err
      end
   end
   
   mainfunc()
--   for k,v in pairs(G) do
      --print(k,v)
--   end
   setmetatable(G, nil)

   for k,v in pairs(temp_global) do
      G[k] = v
   end
   return func, pattern
end
