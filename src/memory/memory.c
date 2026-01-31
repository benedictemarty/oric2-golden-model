/**
 * @file memory.c
 * @brief Memory management implementation (stub)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
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
    /* Nothing to cleanup for now */
    (void)mem;
}

bool memory_load_rom(memory_t* mem, const char* filename, uint16_t offset) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (offset + size > ROM_SIZE) {
        fclose(fp);
        return false;
    }

    size_t read = fread(mem->rom + offset, 1, size, fp);
    fclose(fp);

    return read == (size_t)size;
}

bool memory_load_charset(memory_t* mem, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }

    size_t read = fread(mem->charset, 1, sizeof(mem->charset), fp);
    fclose(fp);

    return read > 0;
}

uint8_t memory_read(memory_t* mem, uint16_t address) {
    /* Zero page and stack */
    if (address < 0x0200) {
        return mem->ram[address];
    }

    /* Main RAM */
    if (address < 0xC000) {
        /* TODO: Handle I/O space ($B400-$BFFF) */
        if (address >= 0x0200 && address < 0xC000) {
            return mem->ram[address];
        }
    }

    /* ROM/Charset area */
    if (address >= 0xC000 && address < 0xE000) {
        return mem->charset[address - 0xC000];
    }

    /* ROM area */
    if (address >= 0xE000) {
        return mem->rom[address - 0xE000];
    }

    return 0xFF;
}

void memory_write(memory_t* mem, uint16_t address, uint8_t value) {
    /* Zero page and stack */
    if (address < 0x0200) {
        mem->ram[address] = value;
        return;
    }

    /* Main RAM */
    if (address < 0xC000) {
        /* TODO: Handle I/O space ($B400-$BFFF) */
        if (address >= 0x0200 && address < 0xC000) {
            mem->ram[address] = value;
        }
        return;
    }

    /* ROM area - writes ignored */
}

uint16_t memory_read_word(memory_t* mem, uint16_t address) {
    uint8_t lo = memory_read(mem, address);
    uint8_t hi = memory_read(mem, address + 1);
    return (hi << 8) | lo;
}

void memory_write_word(memory_t* mem, uint16_t address, uint16_t value) {
    memory_write(mem, address, value & 0xFF);
    memory_write(mem, address + 1, value >> 8);
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
