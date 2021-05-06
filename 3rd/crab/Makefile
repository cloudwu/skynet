all: utf8.so crab.so

utf8.so: lua-utf8.c
	gcc -fPIC --shared -g -O0 -Wall -I/usr/local/include -o $@ $^ -L/usr/local/lib

crab.so: lua-crab.c
	gcc -fPIC --shared -g -O0 -Wall -I/usr/local/include -o $@ $^ -L/usr/local/lib

test: all
	lua test.lua

test1: 
	#./crab words.txt "热爱中国共产党, 响应中央号召"
clean:
	rm crab
