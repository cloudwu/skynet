local skynet = require "skynet"
local server = {}
function server.listen(port,config)
   local web = skynet.newservice("webd","master")
   skynet.call(web,"lua","start",port,config)
end

return server