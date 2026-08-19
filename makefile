CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra -I./src $(shell pkg-config --cflags sdl3 sdl3-ttf sdl3-mixer sdl3-image)
LDFLAGS = $(shell pkg-config --libs sdl3 sdl3-ttf sdl3-mixer sdl3-image) -lm

PREFIX ?= /usr/local
DESTDIR ?=

VERSION = $(shell grep 'BASIKA_VERSION' src/common.h | head -1 | sed 's/.*"\(.*\)".*/\1/')
PACKAGE_NAME = basika-$(VERSION)

SRC = src/main.c src/lexer.c src/interpreter.c src/program.c src/graphics.c src/audio.c
OBJ = $(SRC:.c=.o)
TARGET = basika

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): src/common.h src/lexer.h

.PHONY: test clean install uninstall package

test: $(TARGET)
	./tests/run_tests.sh

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/share/basika/fonts
	install -m 644 fonts/ModernDOS8x16.ttf $(DESTDIR)$(PREFIX)/share/basika/fonts/

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
	cp makefile README.md CHANGELOG.md ERROR_CODES.md KEYWORDS.md $(PACKAGE_NAME)/
	zip -r $(PACKAGE_NAME).zip $(PACKAGE_NAME)
	rm -rf $(PACKAGE_NAME)
	@echo "Created $(PACKAGE_NAME).zip"
