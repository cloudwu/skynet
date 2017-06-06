#!/bin/bash
gcc -o liblua.so -shared lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o \
lfunc.o lgc.o llex.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o \
ltm.o lundump.o lvm.o lzio.o lauxlib.o lbaselib.o lbitlib.o lcorolib.o ldblib.o \
liolib.o lmathlib.o loslib.o lstrlib.o ltablib.o loadlib.o linit.o

