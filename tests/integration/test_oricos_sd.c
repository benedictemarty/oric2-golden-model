/**
 * @file test_oricos_sd.c
 * @brief Test fonctionnel : driver SD bloc OricOS lit une image SD réelle
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Vérifie que le pipeline complet driver SD côté kernel ↔ device émulé
 * Phosphoric fonctionne :
 *   1. Crée image SD test (1 bloc 512 octets, pattern reconnaissable).
 *   2. Charge le kernel OricOS + setup SD device avec l'image.
 *   3. Run jusqu'à STP.
 *   4. ASSERT que `mem[$01:5D40..+512]` contient le pattern (kernel a
 *      lu le bloc 0 via kernel_sd_read_block au boot).
 *
 * Test gated : skip si OricOS/build/kernel.bin absent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "io/sd_device.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define SD_TEST_IMAGE       "/tmp/oricos_sd_test.bin"
#define ORICOS_LOAD_BANK    1
#define ORICOS_LOAD_OFFSET  0x0200u

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
    fflush(stdout); \
    int _b = tests_failed; \
    name(); \
    if (tests_failed == _b) { tests_passed++; printf("PASS\n"); } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected %ld (0x%lX), got %ld (0x%lX)\n", \
               __FILE__, __LINE__, (long)(b), (long)(b), (long)(a), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

/* ─── Context full-system ──────────────────────────────────────────── */

typedef struct {
    cpu65c816_t* cpu;
    via6522_t*   via;
    sd_device_t* sd;
} ctx_t;

static void via_irq_callback(bool state, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (state) cpu816_irq_set(ctx->cpu, IRQF_VIA);
    else       cpu816_irq_clear(ctx->cpu, IRQF_VIA);
}

static uint8_t io_read_callback(uint16_t addr, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0320 && addr <= 0x0327) {
        return sd_read(ctx->sd, (uint8_t)(addr - 0x0320));
    }
    if (addr >= 0x0300 && addr <= 0x03FF) {
        return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    }
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0320 && addr <= 0x0327) {
        sd_write(ctx->sd, (uint8_t)(addr - 0x0320), value);
        return;
    }
    if (addr >= 0x0300 && addr <= 0x03FF) {
        via_write(ctx->via, (uint8_t)(addr & 0x0F), value);
    }
}

/* ─── Helpers (dupliqués de test_oricos_boot.c) ────────────────────── */

static int load_oricos_kernel(memory_t* mem) {
    FILE* fp = fopen(ORICOS_KERNEL_PATH, "rb");
    if (!fp) return -1;
    uint8_t buf[0xE000];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n == 0) return -2;
    for (size_t i = 0; i < n; i++) {
        uint32_t addr24 = ((uint32_t)ORICOS_LOAD_BANK << 16)
                        | (uint32_t)(ORICOS_LOAD_OFFSET + i);
        memory_write24(mem, addr24, buf[i]);
    }
    return 0;
}

static void install_trampolines(memory_t* mem) {
    const uint8_t reset_stub[] = { 0x18, 0xFB, 0x5C, 0x00, 0x02, 0x01 };
    for (size_t i = 0; i < sizeof(reset_stub); i++)
        memory_write24(mem, 0x000100u + i, reset_stub[i]);
    const uint8_t irq_t[] = { 0x5C, 0x00, 0x56, 0x01 };
    for (size_t i = 0; i < sizeof(irq_t); i++)
        memory_write24(mem, 0x000140u + i, irq_t[i]);
    const uint8_t nmi_t[] = { 0x5C, 0x00, 0x55, 0x01 };
    for (size_t i = 0; i < sizeof(nmi_t); i++)
        memory_write24(mem, 0x000130u + i, nmi_t[i]);
    const uint8_t cop_t[] = { 0x5C, 0x00, 0x57, 0x01 };
    for (size_t i = 0; i < sizeof(cop_t); i++)
        memory_write24(mem, 0x000150u + i, cop_t[i]);
    mem->rom[0x3FFC] = 0x00; mem->rom[0x3FFD] = 0x01;
    mem->rom[0x3FFE] = 0x40; mem->rom[0x3FFF] = 0x01;
    mem->rom[0x3FEE] = 0x40; mem->rom[0x3FEF] = 0x01;
    mem->rom[0x3FEA] = 0x30; mem->rom[0x3FEB] = 0x01;
    mem->rom[0x3FFA] = 0x30; mem->rom[0x3FFB] = 0x01;
    mem->rom[0x3FE4] = 0x50; mem->rom[0x3FE5] = 0x01;
    mem->rom[0x3FF4] = 0x50; mem->rom[0x3FF5] = 0x01;
}

/* ─── Test ─────────────────────────────────────────────────────────── */

/* Crée une image SD FAT32 minimale (Sprint 2.j.2/3/4) :
 *   - Bloc 0 (boot sector) : signature + champs BPS/SPC/RSC/NFAT/SPF/ROOT.
 *   - Blocs 1..(FDS-1) : zéros (FAT non utilisée par fat_init/fat_open v0.1).
 *   - Bloc FDS (= 160) : root dir avec 1 entry "HELLO   BIN" (cluster=3,
 *     size=$DEADBEEF=$EFBEADDE LE — distinctif).
 *
 * Champs : BPS=512, SPC=1, reserved=32, num_fats=2, SPF=64, root_cluster=2.
 * → first_data_sector = 32 + 2*64 = 160.
 * Total image : 161 secteurs = 82432 octets.
 */
#define FAT32_TEST_FDS  160u
#define FAT32_TEST_TOTAL_SECTORS  (FAT32_TEST_FDS + 2u)  /* root dir + cluster 3 */

static int create_fat32_test_image(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    uint8_t sec[SD_BLOCK_SIZE];

    /* Bloc 0 : boot sector. */
    memset(sec, 0, SD_BLOCK_SIZE);
    sec[0x0B] = 0x00; sec[0x0C] = 0x02;
    sec[0x0D] = 0x01;
    sec[0x0E] = 0x20; sec[0x0F] = 0x00;
    sec[0x10] = 0x02;
    sec[0x24] = 0x40; sec[0x25] = 0x00; sec[0x26] = 0x00; sec[0x27] = 0x00;
    sec[0x2C] = 0x02; sec[0x2D] = 0x00; sec[0x2E] = 0x00; sec[0x2F] = 0x00;
    memcpy(&sec[0x52], "FAT32   ", 8);
    fwrite(sec, 1, SD_BLOCK_SIZE, f);

    /* Blocs 1..(FDS-1) : zéros, sauf le premier secteur de la FAT à LBA 32.
     * v0.2 : on y écrit une vraie FAT FAT32 partielle pour valider
     *         kernel_fat_next_cluster (chaîne de clusters). */
    memset(sec, 0, SD_BLOCK_SIZE);
    for (unsigned i = 1; i < FAT32_TEST_FDS; i++) {
        if (i == 32) {
            /* LBA 32 = premier secteur de la FAT (FS_RSC = 32). */
            uint8_t fat[SD_BLOCK_SIZE];
            memset(fat, 0, SD_BLOCK_SIZE);
            /* FAT[0] = $0FFFFFF8 (media descriptor) */
            fat[0]=0xF8; fat[1]=0xFF; fat[2]=0xFF; fat[3]=0x0F;
            /* FAT[1] = $0FFFFFFF (reserved) */
            fat[4]=0xFF; fat[5]=0xFF; fat[6]=0xFF; fat[7]=0x0F;
            /* FAT[2] = $0FFFFFF8 (root cluster EOC) */
            fat[8]=0xF8; fat[9]=0xFF; fat[10]=0xFF; fat[11]=0x0F;
            /* FAT[3] = $0FFFFFF8 (HELLO.BIN, single cluster, EOC) */
            fat[12]=0xF8; fat[13]=0xFF; fat[14]=0xFF; fat[15]=0x0F;
            /* FAT[4] = $00000005 (BIG.BIN cluster 4 → 5) */
            fat[16]=0x05; fat[17]=0x00; fat[18]=0x00; fat[19]=0x00;
            /* FAT[5] = $0FFFFFF8 (BIG.BIN EOC) */
            fat[20]=0xF8; fat[21]=0xFF; fat[22]=0xFF; fat[23]=0x0F;
            fwrite(fat, 1, SD_BLOCK_SIZE, f);
        } else {
            fwrite(sec, 1, SD_BLOCK_SIZE, f);
        }
    }

    /* Bloc FDS : root dir avec 1 entry "HELLO   BIN" cluster=3, size=$DEADBEEF. */
    memset(sec, 0, SD_BLOCK_SIZE);
    memcpy(&sec[0x00], "HELLO   BIN", 11);
    sec[0x0B] = 0x20;                  /* archive flag, regular file */
    sec[0x14] = 0x00; sec[0x15] = 0x00; /* cluster_high = 0 */
    sec[0x1A] = 0x03; sec[0x1B] = 0x00; /* cluster_low = 3 LE */
    sec[0x1C] = 0xEF; sec[0x1D] = 0xBE; sec[0x1E] = 0xAD; sec[0x1F] = 0xDE; /* size $DEADBEEF */
    fwrite(sec, 1, SD_BLOCK_SIZE, f);

    /* Bloc FDS+1 : cluster 3 = contenu du fichier "HELLO.BIN".
     * Pour valider read_cluster, on y met le bundle hello (23 bytes :
     * 8B header + 8B section + 7B code). Le reste du secteur = 0. */
    memset(sec, 0, SD_BLOCK_SIZE);
    static const uint8_t hello_bundle[] = {
        /* Header */ 'O', 'O', 'S', 0x01, 0x01, 0x00, 0x01, 0x00,
        /* Section CODE entry */ 0x01, 0x00, 0x07, 0x00, 0x10, 0x00, 0x00, 0x00,
        /* Code app : ldx #'Z' ; lda #1 ; cop #$AA ; rtl */
        0xA2, 'Z', 0xA9, 0x01, 0x02, 0xAA, 0x6B,
    };
    memcpy(sec, hello_bundle, sizeof(hello_bundle));
    fwrite(sec, 1, SD_BLOCK_SIZE, f);

    fclose(f);
    return 0;
}

TEST(test_oricos_sd_read_block_via_kernel_boot) {
    /* Crée image SD test : pattern A..Z répété sur 512 octets. */
    FILE* fimg = fopen(SD_TEST_IMAGE, "wb");
    if (!fimg) {
        printf("FAIL\n    cannot create %s\n", SD_TEST_IMAGE);
        tests_failed++;
        return;
    }
    uint8_t pattern[SD_BLOCK_SIZE];
    for (size_t i = 0; i < SD_BLOCK_SIZE; i++) {
        pattern[i] = (uint8_t)('A' + (i % 26));
    }
    fwrite(pattern, 1, SD_BLOCK_SIZE, fimg);
    fclose(fimg);

    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sd_device_t sd;
    ctx_t ctx = { &cpu, &via, &sd };

    memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);

    int rc = load_oricos_kernel(&mem);
    if (rc == -1) {
        printf("SKIP (kernel.bin absent)                              ");
        memory_cleanup(&mem);
        return;
    }
    ASSERT_EQ(rc, 0);

    install_trampolines(&mem);
    via_init(&via);
    via_reset(&via);
    via_set_irq_callback(&via, via_irq_callback, &ctx);
    sd_init(&sd);
    if (!sd_load_image(&sd, SD_TEST_IMAGE)) {
        printf("FAIL\n    sd_load_image failed\n");
        tests_failed++;
        memory_cleanup(&mem);
        return;
    }
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    int safety = 500000;
    int cycles = 0;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu step error at %02X:%04X\n", cpu.PBR, cpu.PC);
            tests_failed++;
            sd_close(&sd);
            memory_cleanup(&mem);
            return;
        }
        cycles += c;
        via_update(&via, c);
    }
    if (!cpu.stopped) {
        printf("FAIL\n    cpu not stopped after %d cycles, PBR:PC=%02X:%04X\n",
               cycles, cpu.PBR, cpu.PC);
        tests_failed++;
        sd_close(&sd);
        memory_cleanup(&mem);
        return;
    }

    /* Vérifie que kernel_sd_read_block a copié le pattern depuis l'image
     * SD vers $01:5D40 (zone destination du test au boot kernel). */
    for (size_t i = 0; i < SD_BLOCK_SIZE; i++) {
        uint8_t actual = (uint8_t)memory_read24(&mem, 0x015D40u + (uint32_t)i);
        if (actual != pattern[i]) {
            printf("FAIL\n    mem[$01:5D40+%zu] = 0x%02X, expected 0x%02X ('%c')\n",
                   i, actual, pattern[i], pattern[i]);
            tests_failed++;
            sd_close(&sd);
            memory_cleanup(&mem);
            return;
        }
    }

    /* Sprint 2.j.2 : kernel_fat_init a aussi été appelé (boot kernel).
     * Image pattern A..Z n'a PAS la signature "FAT32" → init doit
     * retourner $01 (BAD). Stocké à FS_INIT_RESULT = $016160. */
    ASSERT_EQ((int)memory_read24(&mem, 0x016160), 0x01);

    sd_close(&sd);
    memory_cleanup(&mem);
}

/* Test 2 : image FAT32 minimale, kernel_fat_init doit retourner OK. */
TEST(test_oricos_fat_init_validates_fat32_signature) {
    const char* fat_path = "/tmp/oricos_fat32_test.bin";
    if (create_fat32_test_image(fat_path) < 0) {
        printf("FAIL\n    cannot create %s\n", fat_path);
        tests_failed++;
        return;
    }

    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sd_device_t sd;
    ctx_t ctx = { &cpu, &via, &sd };

    memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);

    int rc = load_oricos_kernel(&mem);
    if (rc == -1) {
        printf("SKIP (kernel.bin absent)                              ");
        memory_cleanup(&mem);
        return;
    }
    ASSERT_EQ(rc, 0);

    install_trampolines(&mem);
    via_init(&via);
    via_reset(&via);
    via_set_irq_callback(&via, via_irq_callback, &ctx);
    sd_init(&sd);
    if (!sd_load_image(&sd, fat_path)) {
        printf("FAIL\n    sd_load_image(%s) failed\n", fat_path);
        tests_failed++;
        memory_cleanup(&mem);
        return;
    }
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    int safety = 500000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) { tests_failed++; sd_close(&sd); memory_cleanup(&mem); return; }
        via_update(&via, c);
    }
    if (!cpu.stopped) {
        printf("FAIL\n    cpu not stopped\n");
        tests_failed++; sd_close(&sd); memory_cleanup(&mem); return;
    }

    /* Note : FS_BUFFER ($015F60) écrasé par kernel_fat_open après
     * kernel_fat_init. La validation de la signature passe par les
     * champs parsés (FS_BPS, FS_SPC, ...) plutôt que le buffer brut. */

    /* FS_INIT_RESULT = $016160 doit être 0 (OK). */
    ASSERT_EQ((int)memory_read24(&mem, 0x016160), 0x00);

    /* Sprint 2.j.3 : champs parsés du boot sector. */
    /* FS_BPS = 512 = $0200 LE → low=$00, high=$02 */
    ASSERT_EQ((int)memory_read24(&mem, 0x016161), 0x00);
    ASSERT_EQ((int)memory_read24(&mem, 0x016162), 0x02);
    /* FS_SPC = 1 */
    ASSERT_EQ((int)memory_read24(&mem, 0x016163), 0x01);
    /* FS_RSC = 32 = $0020 → low=$20, high=$00 */
    ASSERT_EQ((int)memory_read24(&mem, 0x016164), 0x20);
    ASSERT_EQ((int)memory_read24(&mem, 0x016165), 0x00);
    /* FS_NFAT = 2 */
    ASSERT_EQ((int)memory_read24(&mem, 0x016166), 0x02);
    /* FS_SPF = 64 = $00000040 LE */
    ASSERT_EQ((int)memory_read24(&mem, 0x016167), 0x40);
    ASSERT_EQ((int)memory_read24(&mem, 0x016168), 0x00);
    /* FS_ROOT = 2 = $00000002 LE */
    ASSERT_EQ((int)memory_read24(&mem, 0x01616B), 0x02);
    ASSERT_EQ((int)memory_read24(&mem, 0x01616C), 0x00);
    /* FS_FDS = FS_RSC + NFAT * FS_SPF = 32 + 2*64 = 160 = $00A0 LE */
    ASSERT_EQ((int)memory_read24(&mem, 0x01616F), 0xA0);
    ASSERT_EQ((int)memory_read24(&mem, 0x016170), 0x00);

    /* Sprint 2.j.4 : kernel_fat_open("HELLO   BIN") doit avoir trouvé
     * l'entry au bloc FDS (160). FS_OPEN_RESULT = $00 (OK). */
    ASSERT_EQ((int)memory_read24(&mem, 0x01617B), 0x00);
    /* FS_FOUND_CLUSTER = 3 LE (cluster_low=$0003, cluster_high=$0000). */
    ASSERT_EQ((int)memory_read24(&mem, 0x016173), 0x03);
    ASSERT_EQ((int)memory_read24(&mem, 0x016174), 0x00);
    ASSERT_EQ((int)memory_read24(&mem, 0x016175), 0x00);
    ASSERT_EQ((int)memory_read24(&mem, 0x016176), 0x00);
    /* FS_FOUND_SIZE = $DEADBEEF LE = $EF $BE $AD $DE. */
    ASSERT_EQ((int)memory_read24(&mem, 0x016177), 0xEF);
    ASSERT_EQ((int)memory_read24(&mem, 0x016178), 0xBE);
    ASSERT_EQ((int)memory_read24(&mem, 0x016179), 0xAD);
    ASSERT_EQ((int)memory_read24(&mem, 0x01617A), 0xDE);

    /* Sprint 2.j.5 : kernel_fat_read_cluster a copié cluster 3
     * (= bloc FDS+1=161) vers $01:6200. Vérifier les bytes du bundle. */
    ASSERT_EQ((int)memory_read24(&mem, 0x016200), 'O');
    ASSERT_EQ((int)memory_read24(&mem, 0x016201), 'O');
    ASSERT_EQ((int)memory_read24(&mem, 0x016202), 'S');
    ASSERT_EQ((int)memory_read24(&mem, 0x016203), 0x01);
    ASSERT_EQ((int)memory_read24(&mem, 0x016204), 0x01); /* version */
    /* Section CODE entry */
    ASSERT_EQ((int)memory_read24(&mem, 0x016208), 0x01); /* type CODE */
    ASSERT_EQ((int)memory_read24(&mem, 0x01620A), 0x07); /* size lo = 7 */
    /* App code à offset 16 */
    ASSERT_EQ((int)memory_read24(&mem, 0x016210), 0xA2); /* LDX */
    ASSERT_EQ((int)memory_read24(&mem, 0x016211), 'Z');
    ASSERT_EQ((int)memory_read24(&mem, 0x016216), 0x6B); /* RTL */

    /* Sprint 2.j.6 : kernel_app_exec sur le bundle chargé via SD a
     * exécuté l'app, qui a écrit un 'Z' supplémentaire après le 'Z' du
     * bundle inline. CURSOR_ADDR = $BBAB (premier Z, bundle inline) +
     * 1 = $BBAC (Z du bundle SD). */
    ASSERT_EQ((int)memory_read24(&mem, 0x00BBAB), 'Z');
    ASSERT_EQ((int)memory_read24(&mem, 0x00BBAC), 'Z');

    /* Sprint 2.j v0.2 : kernel_fat_next_cluster a lu FAT[4] = 5.
     * FS_NEXT_CLUSTER = $01617C..$01617F, valeur attendue = $00000005. */
    ASSERT_EQ((int)memory_read24(&mem, 0x01617C), 0x05);
    ASSERT_EQ((int)memory_read24(&mem, 0x01617D), 0x00);
    ASSERT_EQ((int)memory_read24(&mem, 0x01617E), 0x00);
    ASSERT_EQ((int)memory_read24(&mem, 0x01617F), 0x00);

    sd_close(&sd);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS SD bloc driver test (Sprint 2.j.1)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_sd_read_block_via_kernel_boot);
    RUN(test_oricos_fat_init_validates_fat32_signature);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
