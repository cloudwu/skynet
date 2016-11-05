PLAT ?= none
PLATS = linux freebsd macosx arm

CC ?= gcc

.PHONY : none $(PLATS) clean all cleanall

#ifneq ($(PLAT), none)

.PHONY : default

default :
	$(MAKE) $(PLAT)

#endif

none :
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

SKYNET_LIBS := -lpthread -lm
SHARED := -fPIC --shared
EXPORT := -Wl,-E
MYCFLAGS := -I../../skynet-src

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd
arm : PLAT = arm

macosx : SHARED := -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT :=
macosx linux arm : SKYNET_LIBS += -ldl
linux freebsd arm : SKYNET_LIBS += -lrt
arm : SKYNET_LIBS += -llinux-atmoic
arm : MYCFLAGS += -DLUA_32BITS


# Turn off jemalloc and malloc hook on macosx

macosx arm : MALLOC_STATICLIB :=
macosx arm : SKYNET_DEFINES :=-DNOUSE_JEMALLOC

linux macosx freebsd arm :
	$(MAKE) all PLAT=$@ SKYNET_LIBS="$(SKYNET_LIBS)" SHARED="$(SHARED)" EXPORT="$(EXPORT)" MALLOC_STATICLIB="$(MALLOC_STATICLIB)" SKYNET_DEFINES="$(SKYNET_DEFINES)" MYCFLAGS="$(MYCFLAGS)"
