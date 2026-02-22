/**
 * @file test_storage.c
 * @brief Storage subsystem tests (Sedoric, FDC)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 1.0.0-alpha
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations for sedoric functions */
typedef struct {
    uint8_t* data;
    uint32_t size;
    uint8_t tracks;
    uint8_t sectors;
    uint8_t sides;
    bool modified;
} sedoric_disk_t;

extern sedoric_disk_t* sedoric_create(void);
extern void sedoric_destroy(sedoric_disk_t* disk);
extern uint8_t* sedoric_get_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector);
extern bool sedoric_read_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector, uint8_t* buffer);
extern bool sedoric_write_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector, const uint8_t* buffer);

/* Forward declarations for FDC */
typedef struct {
    uint8_t status;
    uint8_t command;
    uint8_t track;
    uint8_t sector;
    uint8_t data;
    uint8_t direction;
    bool busy;
    bool drq;
    bool irq;
    uint8_t* disk_data;
    uint32_t disk_size;
    uint8_t tracks;
    uint8_t sectors_per_track;
    uint8_t sector_buf[256];
    int buf_pos;
    int buf_len;
    bool reading;
    bool writing;
} fdc_t;

extern void fdc_init(fdc_t* fdc);
extern void fdc_reset(fdc_t* fdc);
extern void fdc_set_disk(fdc_t* fdc, uint8_t* data, uint32_t size);
extern uint8_t fdc_read(fdc_t* fdc, uint8_t reg);
extern void fdc_write(fdc_t* fdc, uint8_t reg, uint8_t value);

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected 0x%X, got 0x%X\n", __FILE__, __LINE__, (unsigned)(b), (unsigned)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { \
        printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════════ */
/*  SEDORIC TESTS                                                     */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_sedoric_create) {
    sedoric_disk_t* disk = sedoric_create();
    ASSERT_TRUE(disk != NULL);
    ASSERT_TRUE(disk->data != NULL);
    ASSERT_EQ(disk->tracks, 42);
    ASSERT_EQ(disk->sectors, 17);
    ASSERT_FALSE(disk->modified);
    sedoric_destroy(disk);
}

TEST(test_sedoric_sector_rw) {
    sedoric_disk_t* disk = sedoric_create();
    uint8_t write_buf[256];
    uint8_t read_buf[256];
    memset(write_buf, 0xAA, 256);

    ASSERT_TRUE(sedoric_write_sector(disk, 1, 1, write_buf));
    ASSERT_TRUE(disk->modified);

    memset(read_buf, 0, 256);
    ASSERT_TRUE(sedoric_read_sector(disk, 1, 1, read_buf));
    ASSERT_EQ(read_buf[0], 0xAA);
    ASSERT_EQ(read_buf[255], 0xAA);

    sedoric_destroy(disk);
}

TEST(test_sedoric_invalid_sector) {
    sedoric_disk_t* disk = sedoric_create();

    /* Sector 0 is invalid (1-based) */
    ASSERT_TRUE(sedoric_get_sector(disk, 0, 0) == NULL);
    /* Track out of range */
    ASSERT_TRUE(sedoric_get_sector(disk, 50, 1) == NULL);
    /* Sector out of range */
    ASSERT_TRUE(sedoric_get_sector(disk, 0, 20) == NULL);

    uint8_t buf[256];
    ASSERT_FALSE(sedoric_read_sector(disk, 50, 1, buf));

    sedoric_destroy(disk);
}

TEST(test_sedoric_system_info) {
    sedoric_disk_t* disk = sedoric_create();
    uint8_t buf[256];
    ASSERT_TRUE(sedoric_read_sector(disk, 0, 1, buf));
    ASSERT_EQ(buf[0], 'S');
    ASSERT_EQ(buf[1], 'E');
    ASSERT_EQ(buf[2], 'D');
    sedoric_destroy(disk);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  FDC TESTS                                                        */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_fdc_init) {
    fdc_t fdc;
    fdc_init(&fdc);
    ASSERT_EQ(fdc.tracks, 42);
    ASSERT_EQ(fdc.sectors_per_track, 17);
    ASSERT_FALSE(fdc.busy);
}

TEST(test_fdc_reset) {
    fdc_t fdc;
    fdc_init(&fdc);
    fdc.track = 10;
    fdc.sector = 5;
    fdc_reset(&fdc);
    ASSERT_EQ(fdc.track, 0);
    ASSERT_EQ(fdc.sector, 1);
    ASSERT_FALSE(fdc.busy);
    ASSERT_FALSE(fdc.drq);
}

TEST(test_fdc_restore) {
    fdc_t fdc;
    fdc_init(&fdc);
    fdc.track = 20;
    fdc_write(&fdc, 0, 0x00); /* Restore */
    ASSERT_EQ(fdc.track, 0);
    ASSERT_TRUE(fdc.irq);
}

TEST(test_fdc_seek) {
    fdc_t fdc;
    fdc_init(&fdc);
    fdc_write(&fdc, 3, 15); /* DATA = 15 */
    fdc_write(&fdc, 0, 0x10); /* Seek */
    ASSERT_EQ(fdc.track, 15);
}

TEST(test_fdc_read_sector) {
    fdc_t fdc;
    fdc_init(&fdc);
    uint8_t* disk_data = calloc(42 * 17 * 256, 1);
    disk_data[0] = 0xAA;
    disk_data[255] = 0xBB;
    fdc_set_disk(&fdc, disk_data, 42 * 17 * 256);

    fdc.track = 0;
    fdc.sector = 1;
    fdc_write(&fdc, 0, 0x80); /* Read sector */
    ASSERT_TRUE(fdc.reading);

    uint8_t first = fdc_read(&fdc, 3);
    ASSERT_EQ(first, 0xAA);
    for (int i = 1; i < 255; i++) fdc_read(&fdc, 3);
    uint8_t last = fdc_read(&fdc, 3);
    ASSERT_EQ(last, 0xBB);
    ASSERT_FALSE(fdc.reading);
    ASSERT_TRUE(fdc.irq);
    free(disk_data);
}

TEST(test_fdc_write_sector) {
    fdc_t fdc;
    fdc_init(&fdc);
    uint8_t* disk_data = calloc(42 * 17 * 256, 1);
    fdc_set_disk(&fdc, disk_data, 42 * 17 * 256);

    fdc.track = 0;
    fdc.sector = 1;
    fdc_write(&fdc, 0, 0xA0); /* Write sector */
    ASSERT_TRUE(fdc.writing);
    for (int i = 0; i < 256; i++) fdc_write(&fdc, 3, (uint8_t)i);
    ASSERT_FALSE(fdc.writing);
    ASSERT_EQ(disk_data[0], 0x00);
    ASSERT_EQ(disk_data[1], 0x01);
    ASSERT_EQ(disk_data[255], 0xFF);
    free(disk_data);
}

TEST(test_fdc_force_interrupt) {
    fdc_t fdc;
    fdc_init(&fdc);
    fdc.busy = true;
    fdc.reading = true;
    fdc_write(&fdc, 0, 0xD0); /* Force interrupt */
    ASSERT_FALSE(fdc.busy);
    ASSERT_FALSE(fdc.reading);
    ASSERT_TRUE(fdc.irq);
}

TEST(test_fdc_status_read_clears_irq) {
    fdc_t fdc;
    fdc_init(&fdc);
    fdc.irq = true;
    fdc_read(&fdc, 0);
    ASSERT_FALSE(fdc.irq);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  MAIN                                                              */
/* ═══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running Storage tests...\n");
    printf("═══════════════════════════════════════════════════════════\n");

    printf("\n  Sedoric:\n");
    RUN(test_sedoric_create);
    RUN(test_sedoric_sector_rw);
    RUN(test_sedoric_invalid_sector);
    RUN(test_sedoric_system_info);

    printf("\n  FDC WD1793:\n");
    RUN(test_fdc_init);
    RUN(test_fdc_reset);
    RUN(test_fdc_restore);
    RUN(test_fdc_seek);
    RUN(test_fdc_read_sector);
    RUN(test_fdc_write_sector);
    RUN(test_fdc_force_interrupt);
    RUN(test_fdc_status_read_clears_irq);

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
