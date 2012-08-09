LIBNAME = lpeg
LUADIR = /usr/include/lua5.1/

COPT = -O2 -DNDEBUG

CWARNS = -Wall -Wextra -pedantic \
        -Waggregate-return \
	-Wbad-function-cast \
        -Wcast-align \
        -Wcast-qual \
	-Wdeclaration-after-statement \
	-Wdisabled-optimization \
        -Wmissing-prototypes \
        -Wnested-externs \
        -Wpointer-arith \
        -Wshadow \
	-Wsign-compare \
	-Wstrict-prototypes \
	-Wundef \
        -Wwrite-strings \
	#  -Wunreachable-code \


CFLAGS = $(CWARNS) $(COPT) -ansi -I$(LUADIR)
CC = gcc

# For Linux
DLLFLAGS = -shared -fpic
ENV = 

# For Mac OS
# ENV = MACOSX_DEPLOYMENT_TARGET=10.4
# DLLFLAGS = -bundle -undefined dynamic_lookup

lpeg.so: lpeg.o
	env $(ENV) $(CC) $(DLLFLAGS) lpeg.o -o lpeg.so

lpeg.o:		makefile lpeg.c lpeg.h

test: test.lua re.lua lpeg.so
	test.lua

