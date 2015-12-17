-- You should use this module (skynet.coroutine) instead of origin lua coroutine in skynet framework

local coroutine = coroutine
-- origin lua coroutine module
local coroutine_resume = coroutine.resume
local coroutine_yield = coroutine.yield
local coroutine_status = coroutine.status

local select = select
local skynetco = {}

skynetco.create = coroutine.create
skynetco.isyieldable = coroutine.isyieldable
skynetco.running = coroutine.running
skynetco.status = coroutine.status

local skynet_coroutines = setmetatable({}, { __mode = "kv" })

function skynetco.create(f)
	local co = coroutine.create(f)
	-- mark co as a skynet coroutine
	skynet_coroutines[co] = true
	return co
end

function skynetco.isskynetcoroutine(co)
	co = co or coroutine.running()
	return skynet_coroutines[co] ~= nil
end

do -- begin skynetco.resume

	local profile = require "profile"
	-- skynet use profile.resume/yield instead of coroutine.resume/yield
	-- read skynet.lua for detail
	local skynet_resume = profile.resume
	local skynet_yield = profile.yield

	local function unlock(co, ...)
		skynet_coroutines[co] = true
		return ...
	end

	local function skynet_yielding(co, ...)
		skynet_coroutines[co] = false
		return unlock(co, skynet_resume(co, skynet_yield(...)))
	end

	local function resume(co, ok, ...)
		if not ok then
			return ok, ...
		elseif coroutine_status(co) == "dead" then
			-- the main function exit
			skynet_coroutines[co] = nil
			return true, ...
		elseif (...) == "USER" then
			return true, select(2, ...)
		else
			-- blocked in skynet framework, so raise the yielding message
			return resume(co, skynet_yielding(co, ...))
		end
	end

function skynetco.resume(co, ...)
	local co_status = skynet_coroutines[co]
	if not co_status then
		if co_status == false then
			-- is running
			return false, "cannot resume a skynet coroutine suspend by skynet framework"
		end
		if coroutine_status(co) == "dead" then
			-- always return false, "cannot resume dead coroutine"
			return coroutine_resume(co, ...)
		else
			return false, "cannot resume none skynet coroutine"
		end
	end
	return resume(co, coroutine_resume(co, ...))
end

end -- end of skynetco.resume

function skynetco.yield(...)
	return coroutine_yield("USER", ...)
end

do -- begin skynetco.wrap

	local function wrap_co(ok, ...)
		if ok then
			return ...
		else
			error(...)
		end
	end

function skynetco.wrap(f)
	local co = skynetco.create(function(...)
		return f(...)
	end)
	return function(...)
		return wrap_co(skynetco.resume(co, ...))
	end
end

end	-- end of skynetco.wrap

return skynetco
