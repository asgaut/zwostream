ver = debug
platform := $(shell uname -m)

# Raspberry Pi in 32-bit LE mode
ifeq ($(platform), armv7l)
platform := armv7
endif

ifeq ($(platform), aarch64)
platform := armv8
endif

CC = g++

ifeq ($(ver), debug)
DEFS = -D_LIN -D_DEBUG
CFLAGS = -g  -I $(INCLIB) $(DEFS) -lpthread  -DGLIBC_20
else
DEFS = -D_LIN
CFLAGS =  -O3 -I $(INCLIB) $(DEFS) -lpthread  -DGLIBC_20
endif

ifeq ($(platform), x64)
CFLAGS += -m64
CFLAGS += -lrt
endif

CFLAGS += -L./sdk/lib/$(platform) -I./sdk/include -pedantic -Wall -Werror -lopencv_core -lopencv_highgui -lopencv_imgproc

zwostream: main.cpp Makefile
	$(CC) main.cpp -o zwostream $(CFLAGS) -lASICamera2 -Wl,-rpath=. -Wl,-rpath=./sdk/lib/$(platform)

copy_lib:
	cp ./sdk/lib/$(platform)/libASICamera2.so .

clean:
	rm -f zwostream

#pkg-config libusb-1.0 --cflags --libs
#pkg-config opencv --cflags --libs
