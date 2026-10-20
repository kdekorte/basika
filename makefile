CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra -I./src $(shell pkg-config --cflags sdl3 sdl3-ttf sdl3-mixer sdl3-image)

# The SDL libraries load lazily (make LAZY_SDL=0 links them the classic way):
# macOS records them as lazy-load dylibs that dyld opens on the first SDL
# call, and glibc Linux leaves the SDL symbols unresolved until platform.c
# dlopen()s the libraries, so a console-only program neither loads nor needs
# SDL. musl Linux resolves everything at startup and falls back to a normal
# link. Building always needs the SDL development packages.
LAZY_SDL ?= 1
UNAME_S := $(shell uname -s)
SDL_LIBS = $(shell pkg-config --libs sdl3 sdl3-ttf sdl3-mixer sdl3-image)

ifeq ($(LAZY_SDL),1)
ifeq ($(UNAME_S),Darwin)
LDFLAGS = $(shell pkg-config --libs-only-L sdl3 sdl3-ttf sdl3-mixer sdl3-image) \
          -Wl,-lazy-lSDL3 -Wl,-lazy-lSDL3_ttf -Wl,-lazy-lSDL3_mixer -Wl,-lazy-lSDL3_image \
          -lz -lm
else ifeq ($(UNAME_S),Linux)
IS_MUSL = $(shell ldd --version 2>&1 | grep -ci musl)
ifeq ($(IS_MUSL),0)
LDFLAGS = -Wl,--unresolved-symbols=ignore-all -Wl,-z,lazy -ldl -lz -lm
else
LDFLAGS = $(SDL_LIBS) -lz -lm
endif
else
LDFLAGS = $(SDL_LIBS) -lz -lm
endif
else
LDFLAGS = $(SDL_LIBS) -lz -lm
endif

PREFIX ?= /usr/local
DESTDIR ?=

VERSION = $(shell grep 'BASIKA_VERSION' src/common.h | head -1 | sed 's/.*"\(.*\)".*/\1/')
PACKAGE_NAME = basika-$(VERSION)

SRC = src/main.c src/lexer.c src/interpreter.c src/program.c src/graphics.c src/audio.c src/platform.c
OBJ = $(SRC:.c=.o)
TARGET = basika

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): src/common.h src/lexer.h src/platform.h

.PHONY: test test-performance clean install uninstall package burrow-app

test: $(TARGET)
	./tests/run_tests.sh

test-performance: $(TARGET)
	bash tests/scripts/performance_guard.sh

# macOS only: dist/Burrow.app, a standalone app with its own basika and SDL libraries.
burrow-app:
	python3 tools/make_mac_app.py demo/burrow/burrow.bas --name Burrow

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/share/basika/fonts
	install -m 644 fonts/*.ttf fonts/LICENSE-oldschool-pc-fonts.txt $(DESTDIR)$(PREFIX)/share/basika/fonts/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -rf $(DESTDIR)$(PREFIX)/share/basika

package: clean
	rm -rf $(PACKAGE_NAME) $(PACKAGE_NAME).zip
	mkdir -p $(PACKAGE_NAME)/src $(PACKAGE_NAME)/fonts $(PACKAGE_NAME)/demo $(PACKAGE_NAME)/tests
	cp -r src/*.c src/*.h $(PACKAGE_NAME)/src/
	cp -r fonts/* $(PACKAGE_NAME)/fonts/
	cp -r demo/* $(PACKAGE_NAME)/demo/
	cp -r tests/* $(PACKAGE_NAME)/tests/
	cp makefile README.md CHANGELOG.md ERROR_CODES.md KEYWORDS.md LICENSE $(PACKAGE_NAME)/
	zip -r $(PACKAGE_NAME).zip $(PACKAGE_NAME)
	rm -rf $(PACKAGE_NAME)
	@echo "Created $(PACKAGE_NAME).zip"
