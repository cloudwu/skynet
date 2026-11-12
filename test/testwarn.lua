local skynet = require "skynet"

skynet.start(function()
	warn("@on")
	warn("test warning")
	warn("multi-part ", "test ", "warning")
	warn("@off")
	warn("INVISIBLE warning")
	warn("@on")

	setmetatable({}, {
		__gc = function()
			error("error inside __gc")
		end
	})
	collectgarbage("collect")
end)
