-- From the debug console, run: start testunpackn
local skynet = require "skynet"

skynet.start(function()
	local unpackn = assert(skynet.unpackn, "skynet.unpackn is missing")
	local packed = skynet.packstring(nil, "tail")
	local first = table.pack(unpackn(packed, 0, 1))
	assert(first.n == 2 and first[1] == #skynet.packstring(nil) and first[2] == nil)

	local offset, tail = unpackn(packed, first[1], 1)
	assert(offset == #packed and tail == "tail")

	local empty = table.pack(unpackn(packed, first[1], 0))
	assert(empty.n == 1 and empty[1] == first[1])

	local msg, size = skynet.pack("head", "tail")
	local next_offset, head = unpackn(msg, size, 0, 1)
	local end_offset, last = unpackn(msg, size, next_offset, 1)
	skynet.trash(msg, size)
	assert(next_offset == #skynet.packstring("head") and head == "head")
	assert(end_offset == size and last == "tail")

	skynet.error("skynet.unpackn tests passed")
	skynet.fork(skynet.exit)
end)
