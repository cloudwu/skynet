# Use your path
SKYNET_PATH = $(HOME)/skynet
TARGET = $(SKYNET_PATH)/cservice/package.so

$(TARGET) : service_package.c
	gcc -Wall -g --shared -fPIC -o $@ $^ -I$(SKYNET_PATH)/skynet-src

clean :
	rm $(TARGET)
