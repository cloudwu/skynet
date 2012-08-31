local skynet = require "skynet"

local log_level_desc = {
    [0]     = "NOLOG",
    [10]     = "DEBUG",
    [20]      = "INFO",
    [30]   = "WARNING",
    [40]     = "ERROR",
    [50]  = "CRITICAL",
    [60]     = "FATAL",
}

--
-- log object
--
function log_format(self)
    if self.tags and next(self.tags) then
        return string.format("[%s %s *%s*] [%s]%s %s", self.timestamp,self.level,self.name,table.concat(self.tags, ","),self.src,self.msg)
    else
        return string.format("[%s %s *%s*]%s %s", self.timestamp,self.level,self.name,self.src,self.msg)
    end
end

--
-- end log object
--

local function log(name, modname, level, timestamp, msg, src, tags)
	print(log_format {
        name = name,
        modname = modname,
        level = log_level_desc[level],
        timestamp = timestamp,
        msg = msg,
        src = src or '',
        tags = tags,
	})
end

skynet.start(function()
	skynet.dispatch("lua",function(session, from, ...)
		log(...)
	end)
	skynet.register ".lualog"
end)
