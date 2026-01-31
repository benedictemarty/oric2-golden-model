# ORIC-1 Emulator Makefile
# Quick build without CMake

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -I./include
LDFLAGS = -lm

# Debug/Release
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Source files
SOURCES = src/main.c \
          src/cpu/cpu6502.c \
          src/cpu/opcodes.c \
          src/cpu/addressing.c \
          src/memory/memory.c \
          src/memory/banking.c \
          src/io/via6522.c \
          src/io/keyboard.c \
          src/io/cassette.c \
          src/video/video.c \
          src/video/textmode.c \
          src/video/hires.c \
          src/video/renderer.c \
          src/audio/ay3891x.c \
          src/audio/audio_output.c \
          src/storage/tap.c \
          src/storage/sedoric.c \
          src/storage/disk.c \
          src/hostfs/hostfs.c \
          src/hostfs/vfs.c \
          src/utils/logging.c \
          src/utils/config.c

OBJECTS = $(SOURCES:.c=.o)

# Tools
TOOL_SOURCES = tools/bas2tap.c tools/bin2tap.c tools/tap2sedoric.c
TOOL_OBJECTS = src/storage/tap.o src/utils/logging.o

# Targets
TARGET = oric1-emu
TOOLS = bas2tap bin2tap tap2sedoric

.PHONY: all clean tools tests

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

tools: $(TOOLS)

bas2tap: tools/bas2tap.c $(TOOL_OBJECTS)
	$(CC) $(CFLAGS) tools/bas2tap.c $(TOOL_OBJECTS) $(LDFLAGS) -o bas2tap

bin2tap: tools/bin2tap.c $(TOOL_OBJECTS)
	$(CC) $(CFLAGS) tools/bin2tap.c $(TOOL_OBJECTS) $(LDFLAGS) -o bin2tap

tap2sedoric: tools/tap2sedoric.c $(TOOL_OBJECTS)
	$(CC) $(CFLAGS) tools/tap2sedoric.c $(TOOL_OBJECTS) $(LDFLAGS) -o tap2sedoric

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

tests:
	@echo "Building tests..."
	$(CC) $(CFLAGS) tests/unit/test_cpu.c src/cpu/cpu6502.c src/memory/memory.c $(LDFLAGS) -o test_cpu
	$(CC) $(CFLAGS) tests/unit/test_memory.c src/memory/memory.c $(LDFLAGS) -o test_memory
	$(CC) $(CFLAGS) tests/unit/test_io.c src/io/via6522.c $(LDFLAGS) -o test_io
	@echo "Running tests..."
	./test_cpu
	./test_memory
	./test_io

clean:
	rm -f $(OBJECTS) $(TARGET) $(TOOLS)
	rm -f test_cpu test_memory test_io test_video test_audio test_storage
	rm -f tools/*.o

help:
	@echo "ORIC-1 Emulator Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build emulator (default)"
	@echo "  tools      - Build conversion tools"
	@echo "  tests      - Build and run tests"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this help"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1    - Build with debug symbols"
