local skynet = require "skynet"
local mysql = require "mysql"
require "skynet.manager"

local command = {}

local function dump(obj)
    local getIndent, quoteStr, wrapKey, wrapVal, dumpObj
    getIndent = function(level)
        return string.rep("\t", level)
    end
    quoteStr = function(str)
        return '"' .. string.gsub(str, '"', '\\"') .. '"'
    end
    wrapKey = function(val)
        if type(val) == "number" then
            return "[" .. val .. "]"
        elseif type(val) == "string" then
            return "[" .. quoteStr(val) .. "]"
        else
            return "[" .. tostring(val) .. "]"
        end
    end
    wrapVal = function(val, level)
        if type(val) == "table" then
            return dumpObj(val, level)
        elseif type(val) == "number" then
            return val
        elseif type(val) == "string" then
            return quoteStr(val)
        else
            return tostring(val)
        end
    end
    dumpObj = function(obj, level)
        if type(obj) ~= "table" then
            return wrapVal(obj)
        end
        level = level + 1
        local tokens = {}
        tokens[#tokens + 1] = "{"
        for k, v in pairs(obj) do
            tokens[#tokens + 1] = getIndent(level) .. wrapKey(k) .. " = " .. wrapVal(v, level) .. ","
        end
        tokens[#tokens + 1] = getIndent(level - 1) .. "}"
        return table.concat(tokens, "\n")
    end
    return dumpObj(obj, 0)
end

function command.REGISTER(db,account,pwd)
	print("db register account=" .. account .. ";pwd=" .. pwd)
	local sql = string.format("insert into game_users (account,pwd) " .. "values(\'%s\',\'%s\')",account,pwd)
	print("register sql=" .. sql)
	local res = db:query(sql)
end

function command.LOGIN(db,account,pwd)
	print("db login account=" .. account .. ";pwd=" .. pwd)
	local sql = string.format("select numid from game_users where account =\"" .. account .. " \" and pwd=\"" .. pwd .. "\"")
	print("login sql =" .. sql)
	local ret = db:query(sql)
	if(ret.errno == nil)then
		print("login result numid=" .. ret[1].numid)
		return string.format("numid=%d",ret[1].numid)
	end
	return string.format("numid=%d",-1)
	
end

local function test2( db)
    local i=1
    while true do
        local    res = db:query("select * from cats order by id asc")
        print ( "test2 loop times=" ,i,"\n","query result=",dump( res ) )
        res = db:query("select * from cats order by id asc")
        print ( "test2 loop times=" ,i,"\n","query result=",dump( res ) )

        skynet.sleep(1000)
        i=i+1
    end
end
local function test3( db)
    local i=1
    while true do
        local    res = db:query("select * from cats order by id asc")
        print ( "test3 loop times=" ,i,"\n","query result=",dump( res ) )
        res = db:query("select * from cats order by id asc")
        print ( "test3 loop times=" ,i,"\n","query result=",dump( res ) )
        skynet.sleep(1000)
        i=i+1
    end
end
skynet.start(function()
	local db
	local function on_connect(db)
		db:query("set charset utf8");
	end
	local db=mysql.connect({
		host="127.0.0.1",
		port=3306,
		database="mobile_game",
		user="root",
		password="111111",
		max_packet_size = 1024 * 1024,
		on_connect = on_connect
	})
	if not db then
		print("failed to connect")
	end
	print("testmysql success to connect to mysql server")
	--REQUEST:register(db)
	skynet.dispatch("lua",function(session, address, cmd, ...)
			local f= command[string.upper(cmd)]
			if f then
				skynet.ret(skynet.pack(f(db,...)))
			else
				error(string.format("Unknown command %s", tostring(cmd)))
			end
			end)
	skynet.register "MYSQL"
end)

