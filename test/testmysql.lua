local skynet = require "skynet"
local mysql = require "mysql"

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

skynet.start(function()

	local db=mysql.connect{	
		host="127.0.0.1",
		port=3306,
		database="skynet",
		user="root",
		password="1"
	}
	if not db then
		print("failed to connect")
	end
	print("testmysql success to connect to mysql server")

	local res = db:query("drop table if exists cats")
	res = db:query("create table cats " 
		               .."(id serial primary key, ".. "name varchar(5))")
	print( dump( res ) )

	res = db:query("insert into cats (name) "
                             .. "values (\'Bob\'),(\'\'),(null)")
	print ( dump( res ) )

	res = db:query("select * from cats order by id asc")
	print ( dump( res ) )
	
	-- multiresultset test
	res = db:query("select * from cats order by id asc ; select * from cats")
	print ( dump( res ) )

	print ( mysql.quote_sql_str([[\mysql escape %string test'test"]]) )

	-- bad sql statement
	local ok, res = pcall(  db.query, db, "select * from notexisttable" )
	print( "ok= ",ok, dump(res) )

	res = db:query("select * from cats order by id asc")
	print ( dump( res ) )

	skynet.exit()
end)

