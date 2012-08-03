all : master test

master : master.c
	gcc -g -Wall -o $@ $^ -lzmq

test : test.c
	gcc -g -Wall -o $@ $^ -lzmq
