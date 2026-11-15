CC      ?= gcc
CFLAGS  ?= -O3 -march=native -pipe -Wall -Wextra
CFLAGS  += -I/usr/include -I/usr/include/freetype2
LDLIBS  := -lSDL2 -lturbojpeg -lfreetype -lpthread

PREFIX  ?= /usr/local
DESTDIR ?=

TARGET  := ps4view
SRC     := ps4view.c

.PHONY: all clean run install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

run: all
	SDL_VIDEODRIVER=kmsdrm ./$(TARGET) --stats

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -m 755 run.sh $(DESTDIR)$(PREFIX)/bin/ps4view-run

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/bin/ps4view-run
