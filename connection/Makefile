all : test testmap

test : connection.c test.c
	gcc -g -Wall -o $@ $^

testmap : testmap.c map.c
	gcc -g -Wall -o $@ $^

clean :
	rm test testmap


	