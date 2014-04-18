local Skynet =  require("skynet")
local assert = assert
local error  = error
local print = print
local tconcat = table.concat
local tinsert = table.insert
local srep = string.rep
local type = type
local pairs = pairs
local tostring = tostring
local next = next

local logger = {}

-- public field
logger.NOLOG = 0
logger.DEBUG = 10
logger.INFO = 20
logger.WARNING = 30
logger.ERROR = 40
logger.CRITICAL = 50
logger.FATAL = 60

local function log_to_disk(...)
	-- implement your .lualog service
	Skynet.send(".lualog" , "lua" , ...)
end

local function dump_log_to_disk(t)
    for i=1,#t do
		log_to_disk(table.unpack(t[i]))
    end
end

--
-- log helper fun
--

local function get_log_src(level)
    local info = debug.getinfo(level+1, "Sl")
    local src = info.source
    return src .. ":" .. info.currentline .. ":"
end

local starttime = Skynet.starttime()

local function log_timestamp(timestamp)
	local sec = timestamp / 100
	local ms  = timestamp % 100
	local f = os.date("%Y-%m-%d %H:%M:%S",starttime + sec)
	f = string.format("%s.%02d",f,ms)
	return f
end

local function table_serialize(root)
	local cache = {  [root] = "." }
	local function _dump(t,space,name)
		local temp = {}
		for k,v in pairs(t) do
			local key = tostring(k)
			if cache[v] then
				tinsert(temp,"+" .. key .. " {" .. cache[v].."}")
			elseif type(v) == "table" then
				local new_key = name .. "." .. key
				cache[v] = new_key
				tinsert(temp,"+" .. key .. _dump(v,space .. (next(t,k) and "|" or " " ).. srep(" ",#key),new_key))
			else
				tinsert(temp,"+" .. key .. " [" .. tostring(v).."]")
			end
		end
		return tconcat(temp,"\n"..space)
	end
	return (_dump(root, "",""))
end

local function log_format(level, ...)
    local t = {...}
    local out = ''
    for _,v in pairs(t) do
        if level <= logger.DEBUG and type(v) == "table" then
            v_str = "table:" .. table_serialize(v)
        else
            v_str = tostring(v)
        end

        if out == '' then
            out = v_str
        else
            out = out .. "\t" .. v_str
        end
    end
    return out
end

local function tag_table_to_tags(tag_table)
    if tag_table and next(tag_table) then
        local tags = {}
        for k,v in pairs(tag_table) do
            tags[#tags + 1] = k..":"..v
        end
        return tags
    end
end
--
-- end log helper fun
--

--
-- dump list mgr
--
local function _lnew(n)
    local l = {}
    l.tail = 1
    l.len = n
    return l
end

local function _lreset(l)
    l.tail = 1
    return l
end

local function _lpush(l,...)
    l[l.tail] = {...}
    l[l.tail - l.len] = nil
    l.tail = l.tail + 1
end

local function _lempty(l)
    return l.tail == 1
end

local function _lrange(l)
    if l.tail <= l.len then
        return 1, l.tail-1
    end

    return l.tail - l.len, l.tail-1
end

--
-- end dump list mgr
--

function logger:cache(...)
    _lpush(self.dump_list,...)
end

-- 构造符合 lualog 次序的 message, 由 pack_log_message 使用
function logger:get_log_message(level, timestamp, src, ...)
    local msg = log_format(level, ...)
    local modname = self.module_name or self.default_module_name
    local name = self.logger_name or modname
    local timestamp = log_timestamp(timestamp)
    return name, modname, level, timestamp,msg, src, tags
end

function logger:dump()
    if _lempty(self.dump_list) then
        return
    end

    local head, tail = _lrange(self.dump_list)

    local log_message_list = {}
    for i= head,tail do
        log_message_list[#log_message_list + 1] = {self:get_log_message(table.unpack(self.dump_list[i]))}
    end
    dump_log_to_disk(log_message_list)
    _lreset(self.dump_list)
end

function logger:log_i(...)
    log_to_disk(self:get_log_message(...))
end

function logger:log(level, ...)
    -- 过滤掉信息的条件：未设置dump_level，且level低于log_level
    if self.dump_level == logger.NOLOG and level < self.log_level then
        return
    end

    local timestamp = Skynet.now()
    local src = self.log_src and get_log_src(self.stack_level) or ''

    if level < self.log_level then  -- 低于记录级别, 缓存
        self:cache(level, timestamp, src, ...)
    else
        self:log_i(level, timestamp, src, ...) -- 低于dump级别，直接记录
        if self.dump_level ~= logger.NOLOG and level >= self.dump_level then -- dump缓存
            self:dump()
        end
    end
end

--
-- public interface
--
function logger:Debug(...)
    self:log(logger.DEBUG, ...)
end
function logger:Debugf(format, ...)
   self:log(logger.DEBUG, string.format(format, ...))
end

function logger:Info(...)
   self:log(logger.INFO, ...)
end
function logger:Infof(format, ...)
    self:log(logger.INFO, string.format(format, ...))
end

function logger:Warning(...)
    self:log(logger.WARNING, ...)
end
function logger:Warningf(format, ...)
    self:log(logger.WARNING, string.format(format,...))
end

function logger:Error(...)
    self:log(logger.ERROR, ...)
end
function logger:Errorf(format, ...)
    self:log(logger.ERROR, string.format(format,...))
end

function logger:Critical(...)
    self:log(logger.CRITICAL, ...)
end
function logger:Criticalf(format, ...)
    self:log(logger.CRITICAL, string.format(format,...))
end

function logger:Fatal(...)
    self:log(logger.FATAL, ...)
end

function logger:Fatalf(format, ...)
    self:log(logger.FATAL, string.format(format,...))
end

function logger:Assert(v, message)
    if not v then
        self:log(logger.ERROR, "assert:"..tostring(message))
    end
    return assert(v, message)
end

function logger:SError(message, level)
    level = level and level+1 or 2
    self:log(logger.CRITICAL, "error:" .. tostring(message))
    error(message, level)
end

function logger:Tag(key, value)
    self.tag_table[key] = value
    self.tags = tag_table_to_tags(tag_table)
end

function logger:Untag(key)
    self.tag_table[key]=nil
    self.tags = tag_table_to_tags(self.tag_table)
end

function logger:set_modname(name)
    self.module_name = name
end

function logger:config(t)
    if t["name"] ~= nil then
        self.logger_name = t["name"]
    end

    if t["module_name"] ~= nil then
        self.module_name = t["module_name"]
    end

    if t["to_screen"] ~= nil then
        self.to_screen = t["to_screen"]
    end

    if t["level"] ~= nil then
        self.log_level = t["level"]
    end

    if t["log_src"] ~= nil then
        self.log_src = t["log_src"]
    end

    if t["dump_level"] ~= nil then
        self.dump_level = t["dump_level"]
    end

    if self.dump_level ~= logger.NOLOG then
        assert(self.dump_level > self.log_level)
    end
end

function logger:new()
    -- private field
    local obj = {}
    obj.default_module_name = "skynet"
    obj.logger_name = nil
    obj.module_name = nil
    obj.to_screen = true
    obj.log_level = logger.DEBUG
    obj.log_src = true
    obj.dump_level = logger.NOLOG
    obj.dump_num = 100
    self.dump_list = _lnew(obj.dump_num)
    obj.tag_table = {}
    obj.tags = nil
    obj.stack_level = 3

    setmetatable(obj, logger)
    logger.__index = logger
    return obj
end

return logger

