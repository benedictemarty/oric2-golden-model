# ORIC-1 Emulator Makefile
# Complete build system for emulator, tools, and tests

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

# SDL2 support (optional)
SDL2 ?= 0
ifeq ($(SDL2), 1)
    CFLAGS += -DHAS_SDL2 $(shell pkg-config --cflags sdl2 2>/dev/null)
    LDFLAGS += $(shell pkg-config --libs sdl2 2>/dev/null)
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
          src/video/export.c \
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

# Core libraries (no main)
LIB_SOURCES = $(filter-out src/main.c, $(SOURCES))
LIB_OBJECTS = $(LIB_SOURCES:.c=.o)

# Tools
TOOL_OBJECTS = src/storage/tap.o src/utils/logging.o

# Targets
TARGET = oric1-emu
TOOLS = bas2tap bin2tap tap2sedoric

.PHONY: all clean tools tests test-cpu test-memory test-io test-storage test-system test-rom test-video valgrind static-analysis help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

tools: $(TOOLS)

bas2tap: tools/bas2tap.c $(TOOL_OBJECTS)
	$(CC) $(CFLAGS) tools/bas2tap.c $(TOOL_OBJECTS) $(LDFLAGS) -o bas2tap

bin2tap: tools/bin2tap.c $(TOOL_OBJECTS)
	$(CC) $(CFLAGS) tools/bin2tap.c $(TOOL_OBJECTS) $(LDFLAGS) -o bin2tap

tap2sedoric: tools/tap2sedoric.c $(TOOL_OBJECTS) src/storage/sedoric.o
	$(CC) $(CFLAGS) tools/tap2sedoric.c $(TOOL_OBJECTS) src/storage/sedoric.o $(LDFLAGS) -o tap2sedoric

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ═══════════════════════════════════════════════════════════════
#  TESTS
# ═══════════════════════════════════════════════════════════════

TEST_CPU_SRCS = tests/unit/test_cpu.c src/cpu/cpu6502.c src/cpu/opcodes.c \
                src/cpu/addressing.c src/memory/memory.c src/memory/banking.c \
                src/utils/logging.c

TEST_MEM_SRCS = tests/unit/test_memory.c src/memory/memory.c \
                src/memory/banking.c src/utils/logging.c

TEST_IO_SRCS = tests/unit/test_io.c src/io/via6522.c src/utils/logging.c

TEST_STORAGE_SRCS = tests/unit/test_storage.c src/storage/sedoric.c \
                    src/storage/disk.c src/utils/logging.c

TEST_SYSTEM_SRCS = tests/unit/test_full_system.c src/cpu/cpu6502.c \
                   src/cpu/opcodes.c src/cpu/addressing.c src/memory/memory.c \
                   src/memory/banking.c src/io/via6522.c src/utils/logging.c

test-cpu: $(TEST_CPU_SRCS)
	@$(CC) $(CFLAGS) $(TEST_CPU_SRCS) $(LDFLAGS) -o test_cpu
	@./test_cpu

test-memory: $(TEST_MEM_SRCS)
	@$(CC) $(CFLAGS) $(TEST_MEM_SRCS) $(LDFLAGS) -o test_memory
	@./test_memory

test-io: $(TEST_IO_SRCS)
	@$(CC) $(CFLAGS) $(TEST_IO_SRCS) $(LDFLAGS) -o test_io
	@./test_io

test-storage: $(TEST_STORAGE_SRCS)
	@$(CC) $(CFLAGS) $(TEST_STORAGE_SRCS) $(LDFLAGS) -o test_storage
	@./test_storage

test-system: $(TEST_SYSTEM_SRCS)
	@$(CC) $(CFLAGS) $(TEST_SYSTEM_SRCS) $(LDFLAGS) -o test_system
	@./test_system

TEST_ROM_SRCS = tests/unit/test_rom.c src/cpu/cpu6502.c src/cpu/opcodes.c \
                src/cpu/addressing.c src/memory/memory.c src/memory/banking.c \
                src/io/via6522.c src/utils/logging.c

test-rom: $(TEST_ROM_SRCS)
	@$(CC) $(CFLAGS) $(TEST_ROM_SRCS) $(LDFLAGS) -o test_rom
	@./test_rom

TEST_VIDEO_SRCS = tests/unit/test_video.c src/video/video.c src/video/export.c \
                  src/cpu/cpu6502.c src/cpu/opcodes.c src/cpu/addressing.c \
                  src/memory/memory.c src/memory/banking.c src/io/via6522.c \
                  src/utils/logging.c

test-video: $(TEST_VIDEO_SRCS)
	@$(CC) $(CFLAGS) $(TEST_VIDEO_SRCS) $(LDFLAGS) -o test_video
	@./test_video

tests: test-cpu test-memory test-io test-storage test-system test-video
	@echo ""
	@echo "═══════════════════════════════════════════════════════"
	@echo "  All test suites completed!"
	@echo "═══════════════════════════════════════════════════════"

# ═══════════════════════════════════════════════════════════════
#  QUALITY TARGETS
# ═══════════════════════════════════════════════════════════════

STATIC_CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
                -Wdouble-promotion -Wformat=2 -Wundef -Wstrict-prototypes \
                -Wmissing-prototypes -Wold-style-definition -std=c11 -I./include

static-analysis:
	@echo "Running static analysis with extra warnings..."
	@$(CC) $(STATIC_CFLAGS) -fsyntax-only $(LIB_SOURCES) 2>&1 || true
	@echo ""
	@echo "Static analysis complete."

valgrind: test-cpu test-memory test-io test-storage test-system test-rom test-video
	@echo "Running tests under Valgrind..."
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_cpu
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_memory
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_io
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_storage
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_system
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_rom
	@valgrind --leak-check=full --error-exitcode=1 --quiet ./test_video
	@echo ""
	@echo "═══════════════════════════════════════════════════════"
	@echo "  Valgrind: No memory leaks detected!"
	@echo "═══════════════════════════════════════════════════════"

clean:
	rm -f $(OBJECTS) $(TARGET) $(TOOLS)
	rm -f test_cpu test_memory test_io test_storage test_system test_rom test_video
	rm -f tools/*.o

help:
	@echo "ORIC-1 Emulator Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build emulator (default)"
	@echo "  tools        - Build conversion tools"
	@echo "  tests        - Build and run all tests"
	@echo "  test-cpu     - Run CPU tests only"
	@echo "  test-memory  - Run memory tests only"
	@echo "  test-io      - Run VIA/I/O tests only"
	@echo "  test-storage - Run storage tests only"
	@echo "  test-system  - Run integration tests only"
	@echo "  test-rom     - Run ROM compatibility tests"
	@echo "  test-video   - Run video export tests"
	@echo "  valgrind     - Run all tests under Valgrind"
	@echo "  static-analysis - Run static analysis"
	@echo "  clean        - Remove build artifacts"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1      - Build with debug symbols"
	@echo "  SDL2=1       - Build with SDL2 display/audio"
