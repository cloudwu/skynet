local skynet = require "skynet"
local web_interface = require "web.web_interface"
local urllib = require "http.url"
local sweb = {}
local typeclass = {}


local function strsplit(pattern, text)
  local list, pos = {}, 1;

  assert(pattern ~= "", "delimiter matches empty string!");

  while true do
    local first, last, match = text:find(pattern, pos);
    if first then -- found?
      list[#list+1] = text:sub(pos, first-1);
      pos = last+1;
    else
      list[#list+1] = text:sub(pos);
      break;
    end
  end

  return list;
end

function sweb.interface(name,web_root)
   local G = { require = _ENV.require,print=_ENV.print,assert=_ENV.assert }
	if typeclass[name] then
		return true,typeclass[name]
	end

	local si,err = web_interface(name,G,nil,web_root)
	if si then
	local ret = {
		rep = {},
		system = {}
	}

	for _,v in ipairs(si) do
		local id, group, name, f = table.unpack(v)
		ret[group][name] = f
	end

	typeclass[name] = ret
	return true, ret
	else
	return   false,err
	end
end

function sweb.handle(url,method, header, body,web_root)
   local path, query = urllib.parse(url)   
   local q = {}
   if query then
      q = urllib.parse_query(query)
   end
   local path, query = urllib.parse(url)
   path = strsplit("/",path)
   local f = path[#path]
   table.remove(path,#path)
   
   local name = table.concat(path,"/")
   local ok,ret = sweb.interface(name,web_root)
   if not ok then
      return 503,url .."error:"..ret
   end
   local req = {method = method,header=header,body=body,query=q}
   if ret["rep"][f] then
      return ret["rep"][f](req)
   else
      return 503,url ..":has no valid handle"
   end
end


return sweb
