require "protobuf"

addr = io.open("addressbook.pb","rb")
buffer = addr:read "*a"
addr:close()

protobuf.register(buffer)

addressbook = {
	name = "Alice",
	id = 12345,
	phone = {
		{ number = "1301234567" },
		{ number = "87654321", type = "WORK" },
	}
}

code = protobuf.encode("tutorial.Person", addressbook)

decode = protobuf.decode("tutorial.Person" , code)

print(decode.name)
print(decode.id)
for _,v in ipairs(decode.phone) do
	print("\t"..v.number, v.type)
end

phonebuf = protobuf.pack("tutorial.Person.PhoneNumber number","87654321")
buffer = protobuf.pack("tutorial.Person name id phone", "Alice", 123, { phonebuf })
print(protobuf.unpack("tutorial.Person name id phone", buffer))
