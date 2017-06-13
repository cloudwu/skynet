if not _P then
	print[[
This file is examples to show how to inject code into lua service.
It is used to inject into launcher service to change the command.LAUNCH to command.LOGLAUNCH.
telnet the debug_console service (nc 127.0.0.1 8000), and run:
inject 3 examples/injectlaunch.lua	-- 3 means launcher service
]]
	return
end
local command = _P.lua.command

if command.RAWLAUNCH then
	command.LAUNCH, command.RAWLAUNCH = command.RAWLAUNCH
	print "restore command.LAUNCH"
else
	command.RAWLAUNCH = command.LAUNCH
	command.LAUNCH = command.LOGLAUNCH
	print "replace command.LAUNCH"
end
