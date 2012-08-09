lib = require "int64"

local int64 = lib.new
print(lib.tostring(int64 "\1\2\3\4\5\6\7\8"))

a = 1 + int64(1)
b = int64 "\16" + int64("9",10)
print(lib.tostring(a,10), lib.tostring(b,2))
print("+", a+b)
print("-", lib.tostring(a-b,10))
print("*", a*b)
print("/", a/b)
print("%", a%b)
print("^", a^b)
print("==", a == b)
print(">", a > b)
print("#", #a)
