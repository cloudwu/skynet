PLAT ?= none
PLATS = linux freebsd macosx

CC ?= gcc

.PHONY : none $(PLATS) clean all cleanall

ifneq ($(PLAT), none)

.PHONY : default

default :
	$(MAKE) $(PLAT)

endif

none :
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

LIBS := -lpthread -lm
SHARED := -fPIC --shared
EXPORT := -Wl,-E

$(PLATS) : all 

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd

macosx linux : LIBS += -ldl
macosx : SHARED := -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT :=
macosx : MALLOC_STATICLIB :=
macosx : SKYNET_DEFINES :=-DNOUSE_JEMALLOC
linux freebsd : LIBS += -lrt
linux freebsd : MALLOC_STATICLIB = $(JEMALLOC_STATICLIB)
