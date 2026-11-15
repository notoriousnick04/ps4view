CC      ?= gcc
CFLAGS  ?= -O3 -march=native -pipe -Wall -Wextra
CFLAGS  += -I/usr/include -I/usr/include/freetype2
LDLIBS  := -lSDL2 -lturbojpeg -lfreetype -lpthread

PREFIX  ?= /usr/local
DESTDIR ?=

TARGET  := ps4view
SRC     := ps4view.c

.PHONY: all clean run install uninstall check-deps

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

check-deps:
	@echo "Checking dependencies..."
	@missing=0; \
	for pkg in sdl2 freetype2; do \
		if pkg-config --exists $$pkg 2>/dev/null; then echo "  [ok] $$pkg"; \
		else echo "  [MISSING] $$pkg"; missing=1; fi; \
	done; \
	if [ -f /usr/include/turbojpeg.h ]; then echo "  [ok] turbojpeg"; else echo "  [MISSING] turbojpeg (libturbojpeg)"; missing=1; fi; \
	if [ -f /usr/include/linux/videodev2.h ]; then echo "  [ok] v4l2 headers"; else echo "  [MISSING] v4l2 headers"; missing=1; fi; \
	if [ $$missing -ne 0 ]; then \
		echo ""; echo "Install missing packages:"; \
		echo "  Arch:   sudo pacman -S sdl2 freetype2 libjpeg-turbo linux-headers"; \
		echo "  Debian: sudo apt install libsdl2-dev libfreetype-dev libturbojpeg0-dev linux-libc-dev"; \
		echo "  Fedora: sudo dnf install SDL2-devel freetype-devel turbojpeg-devel kernel-headers"; \
		exit 1; \
	fi; \
	echo "All dependencies present."
