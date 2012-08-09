s = require "luaseri"

a = s.pack { hello={3,4}, false, 1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9 }

s.dump(a)

a = s.append(a, 42,4.2,-1,1000,80000,"hello",true,false,nil,"1234567890123456789012345678901234567890")

s.dump(a)
print(a)

function pr(t,...)
	for k,v in pairs(t) do
		print(k,v)
	end
	print(...)
end

print ("------")

local seri, length = s.serialize(a)
print(seri, length)

pr(s.unpack(a))

print("-------")

pr(s.deserialize(seri, length))
