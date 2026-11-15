CC      ?= gcc
CFLAGS  ?= -O3 -march=native -pipe -Wall -Wextra
CFLAGS  += -I/usr/include -I/usr/include/freetype2
LDLIBS  := -lSDL2 -lturbojpeg -lfreetype -lpthread

TARGET  := ps4view
SRC     := ps4view.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

run: all
	SDL_VIDEODRIVER=kmsdrm ./$(TARGET) --stats
