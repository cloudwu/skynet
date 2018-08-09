local skynet = require "skynet"

local names = {
	"cluster",
	"skynet.db.dns",
	"skynet.db.mongo",
	"skynet.db.mysql",
	"skynet.db.redis",
	"sharedata",
	"skynet.socket",
	"sproto"
}

-- set sandbox memory limit to 1M, must set here (at start, out of skynet.start)
skynet.memlimit(1 * 1024 * 1024)

skynet.start(function()
    local a = {}
    local limit
    local ok, err = pcall(function()
        for i=1, 12355 do
            limit = i
            table.insert(a, {})
        end
    end)
    local libs = {}
    for k,v in ipairs(names) do
        local ok, m = pcall(require, v)
        if ok then
            libs[v] = m
        end
    end
    skynet.error(limit, err)
    skynet.exit()
end)
