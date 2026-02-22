/**
 * @file memory.c
 * @brief ORIC-1 Memory management - complete implementation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.3.0-alpha
 *
 * Memory Map:
 * $0000-$00FF: Zero Page
 * $0100-$01FF: Stack
 * $0200-$02FF: System variables
 * $0300-$03FF: I/O area (VIA at $0300-$030F)
 * $0400-$BFFF: User RAM / Screen RAM
 * $C000-$F7FF: BASIC ROM (or RAM overlay)
 * $F800-$FFFF: Monitor ROM (always ROM for vectors)
 */

#include "memory/memory.h"
#include <string.h>
#include <stdio.h>

bool memory_init(memory_t* mem) {
    memset(mem, 0, sizeof(memory_t));
    mem->rom_enabled = true;
    mem->charset_bank = BANK_ROM;
    return true;
}

void memory_cleanup(memory_t* mem) {
    (void)mem;
}

bool memory_load_rom(memory_t* mem, const char* filename, uint16_t offset) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (offset + size > ROM_SIZE) {
        fclose(fp);
        return false;
    }

    size_t rd = fread(mem->rom + offset, 1, (size_t)size, fp);
    fclose(fp);
    return rd == (size_t)size;
}

bool memory_load_charset(memory_t* mem, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return false;

    size_t rd = fread(mem->charset, 1, sizeof(mem->charset), fp);
    fclose(fp);
    return rd > 0;
}

uint8_t memory_read(memory_t* mem, uint16_t address) {
    uint8_t val;

    /* Tracing */
    if (mem->trace_enabled && mem->trace_callback) {
        /* Callback will be called after read */
    }

    /* I/O space: VIA 6522 at $0300-$030F (mirrored in $0300-$03FF) */
    if (address >= 0x0300 && address <= 0x03FF) {
        if (mem->io_read) {
            val = mem->io_read(address, mem->io_userdata);
            if (mem->trace_enabled && mem->trace_callback)
                mem->trace_callback(address, val, MEM_READ);
            return val;
        }
    }

    /* RAM: $0000-$BFFF */
    if (address < 0xC000) {
        val = mem->ram[address];
        if (mem->trace_enabled && mem->trace_callback)
            mem->trace_callback(address, val, MEM_READ);
        return val;
    }

    /* ROM area: $C000-$FFFF - always from rom[] array */
    /* When rom_enabled=false, rom[] acts as RAM overlay.
     * Interrupt vectors ($FFFA-$FFFF) always come from original ROM data. */
    val = mem->rom[address - 0xC000];

    if (mem->trace_enabled && mem->trace_callback)
        mem->trace_callback(address, val, MEM_READ);
    return val;
}

void memory_write(memory_t* mem, uint16_t address, uint8_t value) {
    if (mem->trace_enabled && mem->trace_callback)
        mem->trace_callback(address, value, MEM_WRITE);

    /* I/O space: VIA at $0300-$030F */
    if (address >= 0x0300 && address <= 0x03FF) {
        if (mem->io_write) {
            mem->io_write(address, value, mem->io_userdata);
        }
        return;
    }

    /* RAM: $0000-$BFFF always writable */
    if (address < 0xC000) {
        mem->ram[address] = value;
        return;
    }

    /* ROM overlay area: $C000-$FFFF */
    if (!mem->rom_enabled) {
        /* Write to overlay (stored in rom array when ROM is disabled) */
        mem->rom[address - 0xC000] = value;
    }
    /* Writes to ROM area when ROM enabled are silently ignored */
}

uint16_t memory_read_word(memory_t* mem, uint16_t address) {
    uint8_t lo = memory_read(mem, address);
    uint8_t hi = memory_read(mem, (uint16_t)(address + 1));
    return (uint16_t)((hi << 8) | lo);
}

void memory_write_word(memory_t* mem, uint16_t address, uint16_t value) {
    memory_write(mem, address, (uint8_t)(value & 0xFF));
    memory_write(mem, (uint16_t)(address + 1), (uint8_t)(value >> 8));
}

void memory_set_io_callbacks(memory_t* mem,
                             uint8_t (*read_cb)(uint16_t, void*),
                             void (*write_cb)(uint16_t, uint8_t, void*),
                             void* userdata) {
    mem->io_read = read_cb;
    mem->io_write = write_cb;
    mem->io_userdata = userdata;
}

void memory_set_trace(memory_t* mem, bool enabled,
                     void (*callback)(uint16_t, uint8_t, mem_access_type_t)) {
    mem->trace_enabled = enabled;
    mem->trace_callback = callback;
}

void memory_clear_ram(memory_t* mem, uint8_t pattern) {
    memset(mem->ram, pattern, RAM_SIZE);
}

uint8_t* memory_get_ptr(memory_t* mem, uint16_t address) {
    if (address < RAM_SIZE) {
        return &mem->ram[address];
    }
    return NULL;
}
