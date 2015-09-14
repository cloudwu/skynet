
set(LPEG_SOURCES

lpcap.c  lpcap.h  lpcode.c  lpcode.h  lpprint.c  lpprint.h  lptree.c  lptree.h  lptypes.h  lpvm.c  lpvm.h

)

include_directories(../../skynet-src)

add_library( lpeg OBJECT ${LPEG_SOURCES})


