CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
TARGET   = railway

all: $(TARGET)

$(TARGET): src/main.c
	$(CC) $(CFLAGS) -o $@ $<

run: all
	./$(TARGET)

test: all
	sh tests/smoke_test.sh

# Browser build: needs the Emscripten SDK (emcc). GitHub Actions runs this for you.
web: src/main.c
	emcc src/main.c -O2 -std=c99 -sASYNCIFY -sMODULARIZE=1 -sEXPORT_NAME=createRailway \
		-sEXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 -sENVIRONMENT=web \
		-sEXPORTED_RUNTIME_METHODS=FS,stringToUTF8,UTF8ToString -o web/railway.js

clean:
	rm -f $(TARGET) $(TARGET).exe bookings.txt web/railway.js web/railway.wasm

.PHONY: all run test web clean
