local skynet = require "skynet"
local service = require "skynet.service"
local core = require "skynet.sharetable.core"

local function sharetable_service()
	local skynet = require "skynet"
	local core = require "skynet.sharetable.core"

	local matrix = {}	-- all the matrix
	local files = {}	-- filename : matrix
	local clients = {}

	local sharetable = {}

	local function close_matrix(m)
		if m == nil then
			return
		end
		local ptr = m:getptr()
		local ref = matrix[ptr]
		if ref == nil or ref.count == 0 then
			matrix[ptr] = nil
			m:close()
		end
	end

	function sharetable.loadfile(source, filename, ...)
		close_matrix(files[filename])
		local m = core.matrix("@" .. filename, ...)
		files[filename] = m
		skynet.ret()
	end

	function sharetable.loadstring(source, filename, datasource, ...)
		close_matrix(files[filename])
		local m = core.matrix(datasource, ...)
		files[filename] = m
		skynet.ret()
	end

	local function loadtable(filename, ptr, len)
		close_matrix(files[filename])
		local m = core.matrix([[
			local unpack, ptr, len = ...
			return unpack(ptr, len)
		]], skynet.unpack, ptr, len)
		files[filename] = m
	end

	function sharetable.loadtable(source, filename, ptr, len)
		local ok, err = pcall(loadtable, filename, ptr, len)
		skynet.trash(ptr, len)
		assert(ok, err)
		skynet.ret()
	end

	local function query_file(source, filename)
		local m = files[filename]
		local ptr = m:getptr()
		local ref = matrix[ptr]
		if ref == nil then
			ref = {
				filename = filename,
				count = 0,
				matrix = m,
				refs = {},
			}
			matrix[ptr] = ref
		end
		if ref.refs[source] == nil then
			ref.refs[source] = true
			local list = clients[source]
			if not list then
				clients[source] = { ptr }
			else
				table.insert(list, ptr)
			end
			ref.count = ref.count + 1
		end
		return ptr
	end

	function sharetable.query(source, filename)
		local m = files[filename]
		if m == nil then
			skynet.ret()
			return
		end
		local ptr = query_file(source, filename)
		skynet.ret(skynet.pack(ptr))
	end

	function sharetable.close(source)
		local list = clients[source]
		if list then
			for _, ptr in ipairs(list) do
				local ref = matrix[ptr]
				if ref and ref.refs[source] then
					ref.refs[source] = nil
					ref.count = ref.count - 1
					if ref.count == 0 then
						if files[ref.filename] ~= ref.matrix then
							-- It's a history version
							skynet.error(string.format("Delete a version (%s) of %s", ptr, ref.filename))
							ref.matrix:close()
							matrix[ptr] = nil
						end
					end
				end
			end
			clients[source] = nil
		end
		-- no return
	end

	skynet.dispatch("lua", function(_,source,cmd,...)
		sharetable[cmd](source,...)
	end)

	skynet.info_func(function()
		local info = {}

		for filename, m in pairs(files) do
			info[filename] = {
				current = m:getptr(),
				size = m:size(),
			}
		end

		local function address(refs)
			local keys = {}
			for addr in pairs(refs) do
				table.insert(keys, skynet.address(addr))
			end
			table.sort(keys)
			return table.concat(keys, ",")
		end

		for ptr, copy in pairs(matrix) do
			local v = info[copy.filename]
			local h = v.history
			if h == nil then
				h = {}
				v.history = h
			end
			table.insert(h, string.format("%s [%d]: (%s)", copy.matrix:getptr(), copy.matrix:size(), address(copy.refs)))
		end
		for _, v in pairs(info) do
			if v.history then
				v.history = table.concat(v.history, "\n\t")
			end
		end

		return info
	end)

end

local function load_service(t, key)
	if key == "address" then
		t.address = service.new("sharetable", sharetable_service)
		return t.address
	else
		return nil
	end
end

local function report_close(t)
	local addr = rawget(t, "address")
	if addr then
		skynet.send(addr, "lua", "close")
	end
end

local sharetable = setmetatable ( {} , {
	__index = load_service,
	__gc = report_close,
})

function sharetable.loadfile(filename, ...)
	skynet.call(sharetable.address, "lua", "loadfile", filename, ...)
end

function sharetable.loadstring(filename, source, ...)
	skynet.call(sharetable.address, "lua", "loadstring", filename, source, ...)
end

function sharetable.loadtable(filename, tbl)
	assert(type(tbl) == "table")
	skynet.call(sharetable.address, "lua", "loadtable", filename, skynet.pack(tbl))
end

function sharetable.query(filename)
	local newptr = skynet.call(sharetable.address, "lua", "query", filename)
	if newptr then
		return core.clone(newptr)
	end
end

return sharetable

