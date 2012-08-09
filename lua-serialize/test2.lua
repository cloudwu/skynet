local s = require "luaseri"

addressbook = {
	name = "Alice",
	id = 12345,
	phone = {
		{ number = "1301234567" },
		{ number = "87654321", type = "WORK" },
	}
}

for i=1,100000 do
	local u = s.pack (addressbook)
	local t = s.unpack(u)
end
