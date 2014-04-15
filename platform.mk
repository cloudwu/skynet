PLAT ?= none
PLATS = linux freebsd macosx

CC ?= gcc

.PHONY : none $(PLATS) clean all

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
linux freebsd : LIBS += -lrt
macosx: CJSON_LDFLAGS="-bundle -undefined dynamic_lookup"
linux freebsd: CJSON_LDFLAGS="-shared"

