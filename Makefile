CC=gcc
CFLAGS=-std=c99 -Wall -g -I/usr/include/libuv -I/usr/include/curl
LIBS=-lcurl -luv

TARGET=speedtest

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LIBS)

clean:
	rm -f $(TARGET)
