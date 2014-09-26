#!/bin/sh
cd "$PROJECT_DIR"
echo $1
case $1 in
clean)
/usr/bin/make clean
;;
*)
/usr/bin/make macosx
;;
esac