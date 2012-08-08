all : master test

master : master.c main.c
	gcc -g -Wall -o $@ $^ -lzmq

test : test.c
	gcc -g -Wall -o $@ $^ -lzmq

clean :
	rm master test
