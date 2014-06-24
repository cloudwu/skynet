local skynet = require "skynet"
local mysql = require "mysql"

skynet.start(function()

	local db=mysql.connect{	
		host="192.168.1.218",
		port=3306,
		database="Battle_Data",
		user="root",
		password="1"
	}
	if not db then
		print("failed to connect")
	end
	print("testmysql success to connect to mysql server")

	--local res=db:query("select * from test1;select * from test1")
	local res=db:query("select * from G_BuildData_0 limit 10")
	print(res)
	for k,v in pairs(res) do
		print("k=",k,"v=",v)
		if type(v)=="table" then
			for kk, vv in pairs(v) do
				print("kk=",kk,"vv=",v)
			end
		end
	end
	
	skynet.exit()
end)

