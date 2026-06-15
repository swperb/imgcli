# imgcli - ffmpeg-style image processing in C
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
LDFLAGS ?=
LDLIBS  := -lm

SRC := src/main.c src/image.c src/filters.c src/source.c src/util.c
OBJ := $(SRC:.c=.o)
BIN := imgcli

PREFIX ?= /usr/local

.PHONY: all clean install demo
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# stb headers are large; don't let their warnings clutter the build.
src/image.o: src/image.c
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

# Generate a few sample outputs to eyeball the pipeline.
demo: $(BIN)
	./$(BIN) -y -i testsrc=640x480 demo_card.png
	./$(BIN) -y -i demo_card.png -vf "scale=320:-1,grayscale,edge" demo_edge.png
	./$(BIN) -y -i demo_card.png -vf "gblur=3,contrast=1.3,sepia" demo_stylise.png
	@echo "wrote demo_card.png demo_edge.png demo_stylise.png"
