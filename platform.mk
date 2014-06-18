PLAT ?= none
PLATS = linux freebsd macosx

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

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd

macosx : SHARED := -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT :=
macosx linux : SKYNET_LIBS += -ldl
linux freebsd : SKYNET_LIBS += -lrt

# Turn off jemalloc and malloc hook on macosx

macosx : MALLOC_STATICLIB :=
macosx : SKYNET_DEFINES :=-DNOUSE_JEMALLOC

linux macosx freebsd :
	$(MAKE) all PLAT=$@ SKYNET_LIBS="$(SKYNET_LIBS)" SHARED="$(SHARED)" EXPORT="$(EXPORT)" MALLOC_STATICLIB="$(MALLOC_STATICLIB)" SKYNET_DEFINES="$(SKYNET_DEFINES)"
