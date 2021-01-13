-- skynet module two-step initialize . When you require a skynet module :
-- 1. Run module main function as official lua module behavior.
-- 2. Run the functions register by skynet.init() during the step 1,
--      unless calling `require` in main thread .
-- If you call `require` in main thread ( service main function ), the functions
-- registered by skynet.init() do not execute immediately, they will be executed
-- by skynet.start() before start function.

local M = {}

local mainthread, ismain = coroutine.running()
assert(ismain, "skynet.require must initialize in main thread")

local context = {
	[mainthread] = {},
}

do
	local require = _G.require
	function M.require(...)
		local co, main = coroutine.running()
		if main then
			return require(...)
		else
			local old_init_list = context[co]
			local init_list = {}
			context[co] = init_list
			local ret = require(...)
			for _, f in ipairs(init_list) do
				f()
			end
			context[co] = old_init_list
			return ret
		end
	end
end

function M.init_all()
	for _, f in ipairs(context[mainthread]) do
		f()
	end
	context[mainthread] = nil
end

function M.init(f)
	assert(type(f) == "function")
	local co = coroutine.running()
	table.insert(context[co], f)
end

return M