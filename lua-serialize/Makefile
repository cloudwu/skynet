all : luaseri.so

luaseri.so : serialize.c
	gcc -Wall -fPIC -O2 -D LUAMODULE -o $@ --shared $^ 

clean :
	rm luaseri.so

