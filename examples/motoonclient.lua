package.cpath = "luaclib/?.so"
package.path = "lualib/?.lua;examples/?.lua"
local socket = require "client.socket"

if _VERSION ~= "Lua 5.3" then
	error "Use lua 5.3"
end

local fd
local login_userid
local enter_roomid

local function send_data(cmdid, data)
	local len = string.len(data) + 12
	local package = string.pack("=I2=I2=I4=I4", 0xAA55, len, cmdid, 0x0000) .. data
	socket.send(fd, package)
end

local function recv_data(data)
	local reallen = string.len(data)
	--print(string.format("recv_data...len=%d",reallen))
	local _,len,cmdid,_ = string.unpack("=I2=I2=I4=I4", data)
	--print(string.format("cmdid=%x,len=%d",cmdid,len))
	return cmdid,len-12
end

local function unpack_package(text)
	local size = #text
	if size < 12 then
		return nil, text
	end

	local flag,len = string.unpack("=I2=I2", text)
	--print(string.format("flag = %X,len=%d", flag,len))
	if flag ~= 0xAA55 then
		error(string.format("flag = %X,len=%d", flag,len))
	elseif size < len then
		return nil, text
	end

	return text:sub(1,len), text:sub(1+len)
end

local function recv_package(last)
	local result
	result, last = unpack_package(last)
	if result then
		return result, last
	end
	local r = socket.recv(fd)
	if not r then
		return nil, last
	end
	if r == "" then
		error "Server closed"
	end
	return unpack_package(last .. r)
end

local last = ""
local function request_connect(add,port)
	if fd then
		socket.close(fd)
	end
	io.write("  = \n")
	print("  = connecting " .. add .. ":" .. port)
	io.write("  = \n")
	io.flush()
	fd = assert(socket.connect(add, port))
end

local function request_login(id,apptype,appV,sdkV)
	print(string.format("  = user logining, userid:%d,app:%s,sdk:%s",id,appV,sdkV))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4c16Bc64c16",id,sdkV,apptype,"0",appV)
	send_data(0x0301, data)
	login_userid = id	
end

local function response_login(cmd)
	local timestamp = string.unpack("=I8",cmd)
	print(string.format("  = user login success userid:%d,timestamp:%u",login_userid,timestamp))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_setid(id)
	io.write("  = \n")
	print(string.format("  = user setid userid:%d",id))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	login_userid = id 
end

local function request_enterroom(id,roomid)
	io.write("  = \n")
	print(string.format("  = user entering room, userid:%d,roomid:%d",id,roomid))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local md5str = md5.sumhexa(string.format("%d#######%d",id,roomid))
	local data = string.pack("=I4=I4c32",id,roomid,md5str)
	send_data(0x0303, data)
	enter_roomid = roomid
end

local function request_leaveroom(id,roomid)
	io.write("  = \n")
	print(string.format("  = user leaving room, userid:%d,roomid:%d",id,roomid))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4",id,roomid)
	send_data(0x0304, data)
	request_num_flag = false
end

local function request_roominfo(id,roomid)
	io.write("  = \n")
	print(string.format("  = user querying room info, userid:%d,roomid:%d",id,roomid))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4",roomid,id)
	send_data(0x0308, data)
end

local function response_roominfo(cmd)
	print()
	local id,userid,result = string.unpack("=I4=I4B",cmd)
	print(string.format("  = user query room info %s userid:%d,roomid:%d",((result==0) and "sucess") or "fail",userid,id))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function response_enterroom(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user enter room %s userid:%d,roomid:%d",((result==0) and "sucess") or "fail",login_userid,enter_roomid))
	request_roominfo(login_userid,enter_roomid)
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_room_num(id,roomid)
	local data = string.pack("=I4=I4",roomid,id)
	send_data(0x03BD, data)
end

local function response_leaveroom(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user leave room %s userid:%d,roomid:%d",((result==0) and "sucess") or "fail",login_userid,enter_roomid))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_roomban(id,roomid,bannerid,flag,duration)
	print(string.format("  = user banning, userid:%d,roomid:%d,bannerid:%d,flag:%d,duration:%d",id,roomid,bannerid,flag,duration))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4=I4B=I4",roomid,id,bannerid,flag,duration)
	send_data(0x0505, data)
end

local function request_setnotice(id,roomid,notice)
	print(string.format("  = user setting notice, userid:%d,roomid:%d,notice:%s",id,roomid,notice))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4z",roomid,id,notice)
	send_data(0x0392, data)
end

local function response_setnotice(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user setnotice, userid:%d,roomid:%d,result:%s",login_userid,enter_roomid),((result==0) and "sucess") or "fail")
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_chatmsg(id,roomid,chatmsg)
	print(string.format("  = user send chatmsg, userid:%d,roomid:%d,chatmsg:%s",id,roomid,chatmsg))	
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4=I4=I4BBz",roomid,id,0x0000,0x0000,0,0,string.format("{\"content\":\"%s\",\"identify\":\"1\",\"nickname\":\"一点点\"}",chatmsg))
	send_data(0x0A90, data)
end

local function response_chatmsg(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user chat msg, userid:%d,roomid:%d,result:%s",login_userid,enter_roomid,((result==0) and "sucess") or "fail"))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_queryrtmp(id,roomid,avtype)
	print(string.format("  = user query rtmp, userid:%d,roomid:%d,avtype:%d",id,roomid,avtype))	
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4B",roomid,id,avtype)
	send_data(0x03BA, data)
end

local function response_queryrtmp(cmd)
	local roomid,id,avtype = string.unpack("=I4=I4B",cmd)
	local pos = string.find(cmd,"%[")
	local url = string.sub(cmd,pos)
	print(string.format("  = user query rtmp success, userid:%d,roomid:%d,avtype:%d,url:%s",id,roomid,avtype,url))	
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function response_result(cmd)
	local userid,roomid,ret,answernum,rightnum = string.unpack("=I4=I4B=I4=I4",cmd)
	local result = string.sub(cmd,18)
	--[[
	local info = ""
	for i=1,8 do
		local char = string.byte('A')+i-1
		info = info .. string.char(char) .. ":" .. tostring(string.unpack("<4",result)) .. "\n"
		result = string.sub(result,4)
	end
	]]
	print(string.format("  = user question result, userid:%d,roomid:%d,ret:%d,answernum:%d,rightnum:%d,%s",userid,roomid,ret,answernum,rightnum,result))	
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_turnppt(id,roomid,curpage)
	print(string.format("  = user turning ppt, userid:%d,roomid:%d,curpage:%d",id,roomid,curpage))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4=I2",roomid,id,curpage)
	send_data(0x0501, data)
end

local function response_turnppt(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user turnppt, userid:%d,roomid:%d,result:%s",login_userid,enter_roomid,((result==0) and "sucess") or "fail"))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_setppt(id,roomid,pagenum,url,curpage)
	print(string.format("  = user setting ppt, userid:%d,roomid:%d,pagenum:%d,url:%s,curpage:%d",id,roomid,pagenum,url,curpage))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
	local data = string.pack("=I4=I4=I4=I2z",roomid,id,roomid,pagenum,url)
	send_data(0x0509, data)

	io.write("\n\n")
	io.flush()

	request_turnppt(login_userid,enter_roomid,curpage)
end

local function response_setppt(cmd)
	local result = string.unpack("=I4",cmd)
	print(string.format("  = user setppt, userid:%d,roomid:%d,result:%s",login_userid,enter_roomid,((result==0) and "sucess") or "fail"))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function response_roomban(cmd)
	local result,operator,op = string.unpack("=I4=I4B",cmd)
	print(string.format("  = user ban %s userid:%d,roomid:%d,operator:%d,op:%s",((result==0) and "sucess") or "fail",login_userid,enter_roomid,operator,((op==0) and "confirm") or "cancel"))
	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_queryallcourse()
end

local function request_reloadallcourse(id)
	print(string.format("  = user reload today courses, userid:%d",id))
	io.write("  = \n")
	io.flush()
	send_data(0x03CA, "")

	io.write("  = \n")
	io.write("  = ")
	io.flush()

end

local function request_reloadcourse(id)
	print(string.format("  = user reload all courses, userid:%d",id))
	io.write("  = \n")
	io.flush()
	send_data(0x03CB, "")

	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_uploadlog(id)
	print(string.format("  = user upload log, userid:%d",id))
	io.write("  = \n")
	io.flush()
	local data = string.pack("=I4",id)
	send_data(0x0518, data)

	io.write("  = \n")
	io.write("  = ")
	io.flush()
end

local function request_answer(answer)
	print(string.format("  = user answer:%d",answer))
	io.write("  = \n")
	io.flush()

	local data = string.pack("=I4=I4B",enter_roomid,login_userid,answer)
	send_data(0x03C3, data)

	io.write("  = \n")
	io.write("  = ")
	io.flush()
end


local notify_msg_flag = true 
local request_num_flag = false 
local function notify_msg(msg)
	if notify_msg_flag == true then
		print(msg)	
		io.write("  = \n")
		io.write("  = ")
		io.flush()
	end
end

local function notify_setnotice(cmd)
	local roomid,id,notice = string.unpack("=I4=I4z",cmd)
	notify_msg(string.format("  = notify setting notice, userid:%d,roomid:%d,notice:%s",id,roomid,notice))
end

local function notify_takebreak(cmd)
	local id,userid,duration,result = string.unpack("=I4=I4=I4B",cmd)
	notify_msg(string.format("  = user takeing break %s userid:%d,roomid:%d,duration:%d",((result==0) and "sucess") or "fail",userid,id,duration))
end

local function notify_status(cmdid,cmd)
	local id,userid,status = string.unpack("=I4=I4B",cmd)
	local msg = "unkown"
	local str = "unkown"

	if cmdid == 0x050C then
		msg = "课程状态"
		if status == 0 then
			str = "student can enter"
		elseif status == 1 then
			str = "sooner begin class"
		elseif status == 2 then
			str = "having class"
		elseif status == 3 then
			str = "end taking break"
		elseif status == 4 then
			str = "ended class"
		end
	elseif cmdid == 0x0503 then
		msg = "摄像头开关"
		if status == 0 then
			str = "关闭"
		elseif status == 1 then
			str = "打开"
		elseif status == 2 then
			str = "打开辅视频"
		end
	elseif cmdid == 0x0504 then
		msg = "摄像头调整"
		if status == 0 then
			str = "小屏"
		elseif status == 1 then
			str = "大屏"
		end
	elseif cmdid == 0x0508 then
		msg = "屏幕分享"
		if status == 0 then
			str = "关闭"
		elseif status == 1 then
			str = "打开"
		end
	elseif cmdid == 0x050E then
		msg = "PPT切换"
		if status == 0 then
			str = "关闭"
		elseif status == 1 then
			str = "打开"
		elseif status == 2 then
			str = "切换到辅视频"
		end
	elseif cmdid == 0x050F then
		msg = "举手开关"
		if status == 0 then
			str = "关闭"
		elseif status == 1 then
			str = "允许上麦"
		elseif status == 2 then
			str = "允许视频通话"
		end
	elseif cmdid == 0x03BF then
		msg = "视频布局"
		str = string.format("位置(%d)",status)
	end
	notify_msg(string.format("  = room %s,status:%s, userid:%d,roomid:%d",msg,str,userid,id))
end

local function notify_warnup(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = room warning up, userid:%d,roomid:%d",userid,id))
end

local function notify_canseeav(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = room can see av, userid:%d,roomid:%d",userid,id))
end

local function notify_endclass(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = user end class av, userid:%d,roomid:%d",userid,id))
end

local function notify_teacher_not_inroom(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = room teacher not in room, userid:%d,roomid:%d",userid,id))
end

local function notify_leaveroom(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = user leave room, userid:%d,roomid:%d",userid,id))
end

local function notify_setppt(cmd)
	local id,userid,courseid,pagenum,url = string.unpack("=I4=I4=I4=I2z",cmd)
	notify_msg(string.format("  = user set ppt, userid:%d,roomid:%d,courseid:%d,pagenum:%d,url:%s",userid,id,courseid,pagenum,url))
end

local function notify_turnppt(cmd)
	local id,userid,pageindex = string.unpack("=I4=I4=I2",cmd)
	notify_msg(string.format("  = user turn ppt, userid:%d,roomid:%d,pageindex:%d",userid,id,pageindex))
end

local function notify_brush(cmd)
	local id,userid,pageindex = string.unpack("=I4=I4=I2",cmd)
	notify_msg(string.format("  = user bursh, userid:%d,roomid:%d,pageindex:%d",userid,id,pageindex))
end

local function notify_status_ban(cmd)
	local id,userid,operator,ban,duration = 0
	if string.len(cmd) == 17 then
		id,userid,operator,ban,duration = string.unpack("=I4=I4=I4B=I4",cmd)
	else
		id,userid,operator,ban = string.unpack("=I4=I4=I4B",cmd)
	end

	if operator == 0 then
		notify_msg(string.format("  = room ban all, userid:%d,roomid:%d,%s",userid,id,((ban==0) and "close") or "open"))
	else
		notify_msg(string.format("  = room ban one, userid:%d,roomid:%d,operator:%d,duration:%d,%s",userid,id,operator,duration,((ban==0) and "open") or "close"))
	end
end

local function notify_sharevideo_url(cmd)
	local id,userid,url = string.unpack("=I4=I4z",cmd)
	notify_msg(string.format("  = room share video url, userid:%d,roomid:%d,url:%s",userid,id,url))
end

local function notify_stickmsg(cmd)
	local id,userid,stickid = string.unpack("=I4=I4=I4",cmd)
	local content = string.sub(cmd,13)
	notify_msg(string.format("  = room stick msg, userid:%d,roomid:%d,stickerid:%d,msg:%s",userid,id,stickid,content))
end

local function notify_del_back(cmd)
	local id,userid,pageindex,op = string.unpack("=I4=I4=I2=I4",cmd)
	notify_msg(string.format("  = room del or back, userid:%d,roomid:%d,pageindex:%d,op:%s",userid,id,pageindex,((op==0) and "all brush") or "cur brush"))
end

local function notify_light_pen(cmd)
	local id,userid = string.unpack("=I4=I4",cmd)
	notify_msg(string.format("  = room light pen, userid:%d,roomid:%d",userid,id))
end

local function notify_hand_updown(cmd)
	local id,userid,status = string.unpack("=I4=I4B",cmd)
	notify_msg(string.format("  = room hand up down, userid:%d,roomid:%d,%s",userid,id,((status==0) and "up") or "down"))
end

local function notify_agree_refuse_speak(cmd)
	local id,userid,operator,status = string.unpack("=I4=I4=I4B",cmd)
	notify_msg(string.format("  = room agree refuse speak, userid:%d,roomid:%d,operator:%d,%s",userid,id,operator,((status==0) and "agree") or "refuse"))
end

local function notify_agree_refuse_videocall(cmd)
	local id,userid,operator,status = string.unpack("=I4=I4=I4B",cmd)
	notify_msg(string.format("  = room agree refuse video call, userid:%d,roomid:%d,operator:%d,%s",userid,id,operator,((status==0) and "agree") or "refuse"))
end

local function notify_quality(cmd)
	local id,userid,operator = string.unpack("=I4=I4=I4",cmd)
	notify_msg(string.format("  = room video call quality not ok, userid:%d,roomid:%d,operator:%d",userid,id,operator))
end

local function notify_room_num(cmd)
	local id,userid,num = string.unpack("=I4=I4=I4",cmd)
	notify_msg(string.format("  = room num, userid:%d,roomid:%d,num:%d",userid,id,num))
end

local function notify_room_msg(cmd)
	local id,userid,identifyid,receiverid,typeid,bcompress = string.unpack("=I4=I4=I4=I4BB",cmd)
	local msg = string.sub(cmd,19)
	if bcompress == 1 then
		local uncompress = zlib.inflate()
		local compress = zlib.deflate()
		msg,eof,bytes_in,bytes_out = uncompress(msg,"sync")
	end
	--local posbegin = string.find(msg,"content") + string.len("content\"")
	local posbegin = string.find(msg,":")
	local posend = string.find(msg,"[%,%}]",posbegin)
	local content = string.sub(msg,posbegin+1,posend-1)
	notify_msg(string.format("  = room msg, userid:%d,roomid:%d,contnet:%s",userid,id,content))
end

function trim(src,sep)
	local ret={}
	local index=1
	for v in string.gmatch(src,sep) do
		ret[index] = v 
		index = index+1
	end 
	return ret 
end

local function do_response(cmdid,cmd)
	if cmdid == 0x8301 then
		response_login(cmd)
	elseif cmdid == 0x8303 then
		response_enterroom(cmd)
	elseif cmdid == 0x8304 then
		response_leaveroom(cmd)
	elseif cmdid == 0x8308 then
		response_roominfo(cmd)
	elseif cmdid == 0x8505 then
		response_roomban(cmd)
	elseif cmdid == 0x8501 then
		response_turnppt(cmd)
	elseif cmdid == 0x8509 then
		response_setppt(cmd)
	elseif cmdid == 0x0514 then
		notify_takebreak(cmd)
	elseif cmdid == 0x0522 then
		notify_warnup(cmd)
	elseif cmdid == 0x0390 then
		notify_canseeav(cmd)
	elseif cmdid == 0x0391 then
		notify_endclass(cmd)
	elseif cmdid == 0x0392 then
		notify_setnotice(cmd)
	elseif cmdid == 0x8392 then
		response_setnotice(cmd)
	elseif cmdid == 0x0520 then
		notify_teacher_not_inroom(cmd)
	elseif cmdid == 0x0304 then
		notify_leaveroom(cmd)
	elseif cmdid == 0x0509 then
		notify_setppt(cmd)
	elseif cmdid == 0x0501 then
		notify_turnppt(cmd)
	elseif cmdid == 0x0502 then
		notify_brush(cmd)
	elseif cmdid == 0x0503 then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x0504 then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x0505 then
		notify_status_ban(cmd)
	elseif cmdid == 0x0506 then
		notify_del_back(cmd)
	elseif cmdid == 0x0508 then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x050C then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x050E then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x050F then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x03BF then
		notify_status(cmdid,cmd)
	elseif cmdid == 0x050A then
		notify_light_pen(cmd)
	elseif cmdid == 0x0510 then
		notify_hand_updown(cmd)
	elseif cmdid == 0x0511 then
		notify_agree_refuse_speak(cmd)
	elseif cmdid == 0x0512 then
		notify_stickmsg(cmd)
	elseif cmdid == 0x0513 then
		notify_sharevideo_url(cmd)
	elseif cmdid == 0x03BE then
		notify_agree_refuse_videocall(cmd)
	elseif cmdid == 0x03C5 then
		notify_quality(cmd)
	elseif cmdid == 0x83BD then
		notify_room_num(cmd)
	elseif cmdid == 0x0A90 then
		notify_room_msg(cmd)
	elseif cmdid == 0x8A90 then
		response_chatmsg(cmd)
	elseif cmdid == 0x83BA then
		response_queryrtmp(cmd)
	elseif cmdid == 0x83C4 then
		response_result(cmd)
	else
		print(string.format("  =  respone not understand \"%04x\"",cmdid))
		io.write("  = ")
		io.flush()
	end
end

local curcmdid
local cmdlen 
local out_file = io.open(string.format("./data.%s",os.date("%y%m%d%H%M%S")),"a+")
local function dispatch_package()
	while fd do
		local v
		v, last = recv_package(last)
		if not v then
			break
		end

		curcmdid,cmdlen = recv_data(v)
		if curcmdid ~= 0x8000 then
			for i=1,string.len(v),1 do
				out_file:write(string.format("%02X ",string.byte(v,i)))
			end
			out_file:write("\n\n")
			out_file:flush()

			if curcmdid then
				do_response(curcmdid,string.sub(v,13,cmdlen+13))
			end
		end
	end
end

local help_info="\
	login: ip port userid(normal:106.75.14.92)\
	setid: userid\
	enterroom: roomid\
	banuser: bannerid duration\
	unbanuser: bannerid\
	setnotice:  notice(like \"同学们不要着急,我们正在解决问题\")\
	setppt:  pagenum url(http://auv.res.costoon.com/live/640p/20170302yicp) curpage\
	turnppt:  curpage\
	chatmsg:  msg(like \"大家好,我是大鹏\")\
	uploadlog: userid\
	closenotify:\
	opennotify:\
	queryrtmp:\
	reloadallcourse:\
	reloadcourse:\
	shownum:\
	noshownum:\
"
io.write("  = ")
io.flush()
local timer_heart_beat = 0
local timer_query_num = 0

while true do
	dispatch_package()
	local cmdline = socket.readstdin()
	--if cmdline and string.find(cmdline,"not understand")==nil and string.find(cmdline,"log")==nil then
	if cmdline and cmdline ~= "  = " then
		--local tb = trim(cmdline,"[%w%.%?%:%/%l]+")	
		local firstpos = string.find(cmdline,"[^%s]")
		if firstpos then
			local line = string.sub(cmdline,firstpos)
			local tb = trim(line,"[^%s]*")	
			local cmd = tb[1]
			if cmd then
				if cmd == "quit" then
					break	
				elseif cmd =="help" or cmd == "?" then
					print(help_info)
					io.write("  = ")
					io.flush()
				elseif cmd == "login" and tb[2] ~= nil and tb[3] ~= nil then
					request_connect(tb[2],tb[3])
					request_login(tb[4],2,"1.4.1","1.6.1")
				elseif cmd == "setid" and tb[2] ~= nil then
					request_setid(tb[2])
				elseif cmd == "closenotify" then
					io.write("  = ")
					io.flush()
					notify_msg_flag = false
				elseif cmd == "opennotify" then
					io.write("  = ")
					io.flush()
					notify_msg_flag = true 
				elseif cmd == "shownum" then
					io.write("  = ")
					io.flush()
					request_num_flag = true 
				elseif cmd == "noshownum" then
					io.write("  = ")
					io.flush()
					request_num_flag = false
				elseif cmd == "enterroom" and tb[2] ~= nil then
					request_enterroom(login_userid,tb[2])
				elseif cmd == "leaveroom" then
					request_leaveroom(login_userid,enter_roomid)
				elseif cmd == "banuser" and tb[2] ~= nil and tb[3] ~= nil then
					request_roomban(login_userid,enter_roomid,tb[2],1,tb[3])
				elseif cmd == "unbanuser" and tb[2] ~= nil then
					request_roomban(login_userid,enter_roomid,tb[2],0,0)
				elseif cmd == "setnotice" and tb[2] ~= nil then
					request_setnotice(login_userid,enter_roomid,tb[2])
				elseif cmd == "setppt" and tb[2] ~= nil and tb[3] ~= nil then
					request_setppt(login_userid,enter_roomid,tb[2],tb[3],tb[4])
				elseif cmd == "turnppt" and tb[2] ~= nil then
					request_turnppt(login_userid,enter_roomid,tb[2])
				elseif cmd == "chatmsg" and tb[2] ~= nil then
					request_chatmsg(login_userid,enter_roomid,tb[2])
				elseif cmd == "queryrtmp" and tb[2] ~= nil then
					request_queryrtmp(login_userid,enter_roomid,tb[2])
				elseif cmd == "reloadallcourse" then
					request_reloadallcourse(login_userid)
				elseif cmd == "reloadcourse" then
					request_reloadcourse(login_userid)
				elseif cmd == "uploadlog" and tb[2] ~= nil then
					request_uploadlog(tb[2])
				elseif cmd == "answer" and tb[2] ~= nil then
					request_answer(tb[2])
				else
					print("  =  request not understand \"" .. cmdline .. "\"")
					print(help_info)
					io.write("  = ")
					io.flush()
				end
			end
		else
			io.write("  = \n")
			io.write("  = ")
			io.flush()
		end
	else
		socket.usleep(100)
		timer_heart_beat = timer_heart_beat + 1
		timer_query_num = timer_query_num + 1
		if fd and timer_heart_beat >= 10000 then
			send_data(0x0000,"")
			timer_heart_beat = 0
		end

		if login_userid and enter_roomid then
			if request_num_flag and fd and timer_query_num == 2*10000 then
				request_room_num(login_userid,enter_roomid)
				timer_query_num = 0
			end
		end
	end
end
