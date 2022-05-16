-- You should use this module (skynet.coroutine) instead of origin lua coroutine in skynet framework

local coroutine = coroutine
-- origin lua coroutine module
local coroutine_resume = coroutine.resume
local coroutine_yield = coroutine.yield
local coroutine_status = coroutine.status
local coroutine_running = coroutine.running
local coroutine_close = coroutine.close

local select = select
local skynetco = {}

skynetco.isyieldable = coroutine.isyieldable
skynetco.running = coroutine.running
skynetco.status = coroutine.status

local skynet_coroutines = setmetatable({}, { __mode = "kv" })
-- true : skynet coroutine
-- false : skynet suspend
-- nil : exit

function skynetco.create(f)
	local co = coroutine.create(f)
	-- mark co as a skynet coroutine
	skynet_coroutines[co] = true
	return co
end

do -- begin skynetco.resume
	local function unlock(co, ...)
		skynet_coroutines[co] = true
		return ...
	end

	local function skynet_yielding(co, ...)
		skynet_coroutines[co] = false
		return unlock(co, coroutine_resume(co, coroutine_yield(...)))
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

	-- record the root of coroutine caller (It should be a skynet thread)
	local coroutine_caller = setmetatable({} , { __mode = "kv" })

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
		local from = coroutine_running()
		local caller = coroutine_caller[from] or from
		coroutine_caller[co] = caller
		return resume(co, coroutine_resume(co, ...))
	end

	function skynetco.thread(co)
		co = co or coroutine_running()
		if skynet_coroutines[co] ~= nil then
			return coroutine_caller[co] , false
		else
			return co, true
		end
	end

end -- end of skynetco.resume

function skynetco.status(co)
	local status = coroutine_status(co)
	if status == "suspended" then
		if skynet_coroutines[co] == false then
			return "blocked"
		else
			return "suspended"
		end
	else
		return status
	end
end

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

function skynetco.close(co)
	skynet_coroutines[co] = nil
	return coroutine_close(co)
end

return skynetco
