/**
 * @file test_cast.c
 * @brief Cast server unit tests
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-28
 * @version 1.2.0-alpha
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* stb header only (implementation is in cast_server.c) */
#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb_image_write.h"

#include "network/cast_server.h"

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
/*  JPEG CALLBACK FOR TESTS                                            */
/* ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t* data;
    int      len;
    int      capacity;
} test_jpeg_buf_t;

static void test_jpeg_write_cb(void* context, void* data, int size) {
    test_jpeg_buf_t* buf = (test_jpeg_buf_t*)context;
    if (buf->len + size > buf->capacity) {
        int new_cap = (buf->len + size) * 2;
        if (new_cap < 4096) new_cap = 4096;
        uint8_t* new_data = (uint8_t*)realloc(buf->data, (size_t)new_cap);
        if (!new_data) return;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->len, data, (size_t)size);
    buf->len += size;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 1: UPSCALE SINGLE PIXEL                                      */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_upscale_single_pixel) {
    uint8_t src[3] = {0xFF, 0x00, 0x80};
    uint8_t dst[3 * 9] = {0}; /* 3x3 = 9 pixels */

    cast_upscale_nearest(src, 1, 1, dst, 3);

    /* All 9 pixels should be the same color */
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(dst[i * 3 + 0], 0xFF);
        ASSERT_EQ(dst[i * 3 + 1], 0x00);
        ASSERT_EQ(dst[i * 3 + 2], 0x80);
    }
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 2: UPSCALE 2x2 FRAME                                         */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_upscale_2x2_frame) {
    /* 2x2 source: R, G, B, W */
    uint8_t src[2 * 2 * 3] = {
        0xFF, 0x00, 0x00,  0x00, 0xFF, 0x00,  /* row 0: red, green */
        0x00, 0x00, 0xFF,  0xFF, 0xFF, 0xFF   /* row 1: blue, white */
    };
    uint8_t dst[4 * 4 * 3]; /* 4x4 with factor 2 */

    cast_upscale_nearest(src, 2, 2, dst, 2);

    /* Top-left 2x2 block should be red */
    ASSERT_EQ(dst[0], 0xFF); ASSERT_EQ(dst[1], 0x00); ASSERT_EQ(dst[2], 0x00);
    /* (1,0) in dst = still red (upscaled) */
    ASSERT_EQ(dst[3], 0xFF); ASSERT_EQ(dst[4], 0x00); ASSERT_EQ(dst[5], 0x00);
    /* (2,0) in dst = green */
    ASSERT_EQ(dst[6], 0x00); ASSERT_EQ(dst[7], 0xFF); ASSERT_EQ(dst[8], 0x00);

    /* Row 2 (y=2 in dst) should be blue, blue, white, white */
    int row2_off = 2 * 4 * 3; /* y=2, 4 pixels wide */
    ASSERT_EQ(dst[row2_off + 0], 0x00);
    ASSERT_EQ(dst[row2_off + 1], 0x00);
    ASSERT_EQ(dst[row2_off + 2], 0xFF); /* blue */
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 3: UPSCALE COLOR PRESERVATION                                 */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_upscale_color_preservation) {
    /* Single pixel with specific ORIC-like color */
    uint8_t src[3] = {0x00, 0xFF, 0x00}; /* green */
    uint8_t dst[3 * 4]; /* factor 2: 2x2 = 4 pixels */

    cast_upscale_nearest(src, 1, 1, dst, 2);

    /* All 4 pixels must be green */
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(dst[i * 3 + 0], 0x00);
        ASSERT_EQ(dst[i * 3 + 1], 0xFF);
        ASSERT_EQ(dst[i * 3 + 2], 0x00);
    }
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 4: JPEG ENCODE NON-ZERO LENGTH                                */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_jpeg_encode_nonzero) {
    int w = 8, h = 8;
    uint8_t img[8 * 8 * 3];
    for (int i = 0; i < w * h * 3; i++) {
        img[i] = (uint8_t)(i & 0xFF);
    }

    test_jpeg_buf_t jbuf = {NULL, 0, 0};
    stbi_write_jpg_to_func(test_jpeg_write_cb, &jbuf, w, h, 3, img, 80);

    ASSERT_TRUE(jbuf.len > 0);

    free(jbuf.data);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 5: JPEG SOI MARKER                                            */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_jpeg_soi_marker) {
    int w = 4, h = 4;
    uint8_t img[4 * 4 * 3];
    memset(img, 0x80, sizeof(img));

    test_jpeg_buf_t jbuf = {NULL, 0, 0};
    stbi_write_jpg_to_func(test_jpeg_write_cb, &jbuf, w, h, 3, img, 80);

    /* JPEG files start with SOI marker: 0xFF 0xD8 */
    ASSERT_TRUE(jbuf.len >= 2);
    ASSERT_EQ(jbuf.data[0], 0xFF);
    ASSERT_EQ(jbuf.data[1], 0xD8);

    free(jbuf.data);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 6: SERVER LIFECYCLE                                           */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_server_lifecycle) {
    cast_server_t server;

    /* Use a high port to avoid conflicts */
    bool ok = cast_server_init(&server, 18080);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(server.active);
    ASSERT_TRUE(server.thread_running);
    ASSERT_EQ(server.port, 18080);

    cast_server_stop(&server);
    ASSERT_FALSE(server.active);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 7: SERVER PORT ZERO DEFAULT                                   */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_server_port_default) {
    cast_server_t server;

    bool ok = cast_server_init(&server, 0);
    ASSERT_TRUE(ok);
    ASSERT_EQ(server.port, CAST_DEFAULT_PORT);

    cast_server_stop(&server);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 8: HTTP HTML PAGE CONTENT                                     */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_http_html_page) {
    cast_server_t server;
    bool ok = cast_server_init(&server, 18081);
    ASSERT_TRUE(ok);

    /* Connect to server and request / */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(sock >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(18081);

    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    ASSERT_EQ(ret, 0);

    const char* request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sock, request, strlen(request), 0);

    /* Wait for response */
    usleep(100000); /* 100ms */
    char response[4096] = {0};
    recv(sock, response, sizeof(response) - 1, 0);

    /* Check for HTML content */
    ASSERT_TRUE(strstr(response, "text/html") != NULL);
    ASSERT_TRUE(strstr(response, "ORIC-1 Cast") != NULL);
    ASSERT_TRUE(strstr(response, "/stream") != NULL);

    close(sock);
    cast_server_stop(&server);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 9: HTTP MJPEG STREAM HEADER                                   */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_http_mjpeg_stream) {
    cast_server_t server;
    bool ok = cast_server_init(&server, 18082);
    ASSERT_TRUE(ok);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(sock >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(18082);

    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    ASSERT_EQ(ret, 0);

    const char* request = "GET /stream HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sock, request, strlen(request), 0);

    usleep(100000);
    char response[4096] = {0};
    recv(sock, response, sizeof(response) - 1, 0);

    /* Check MJPEG boundary header */
    ASSERT_TRUE(strstr(response, "multipart/x-mixed-replace") != NULL);
    ASSERT_TRUE(strstr(response, "boundary=frame") != NULL);

    close(sock);
    cast_server_stop(&server);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 10: MDNS QUERY PACKET CONSTRUCTION                           */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_mdns_query_build) {
    uint8_t buf[128];
    int len = cast_build_mdns_query(buf, sizeof(buf));

    /* Should be a valid length */
    ASSERT_TRUE(len > 12); /* At least header + question */

    /* DNS Header: QDCOUNT = 1 */
    ASSERT_EQ(buf[4], 0);
    ASSERT_EQ(buf[5], 1);

    /* First label: 11 bytes = "_googlecast" */
    ASSERT_EQ(buf[12], 11);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 11: MDNS QUERY LENGTH                                        */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_mdns_query_length) {
    uint8_t buf[128];
    int len = cast_build_mdns_query(buf, sizeof(buf));

    /* Expected: 12 (header) + 1+11 + 1+4 + 1+5 + 1 (null) + 4 (type+class) = 40 */
    ASSERT_EQ(len, 40);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 12: MDNS QUERY BUFFER TOO SMALL                               */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_mdns_query_too_small) {
    uint8_t buf[10]; /* Too small */
    int len = cast_build_mdns_query(buf, sizeof(buf));

    ASSERT_EQ(len, -1);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST 13: FRAME PUSH SETS READY FLAG                                */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_frame_push_ready) {
    cast_server_t server;
    bool ok = cast_server_init(&server, 18083);
    ASSERT_TRUE(ok);

    /* Initially no frame ready */
    ASSERT_FALSE(server.frame_ready);

    /* Push a small test frame */
    uint8_t frame[240 * 224 * 3];
    memset(frame, 0x42, sizeof(frame));
    cast_server_push_frame(&server, frame, 240, 224);

    /* Frame should be flagged as ready */
    pthread_mutex_lock(&server.mutex);
    ASSERT_TRUE(server.frame_ready);
    pthread_mutex_unlock(&server.mutex);

    cast_server_stop(&server);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  MAIN                                                               */
/* ═══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Cast Server Unit Tests\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_upscale_single_pixel);
    RUN(test_upscale_2x2_frame);
    RUN(test_upscale_color_preservation);
    RUN(test_jpeg_encode_nonzero);
    RUN(test_jpeg_soi_marker);
    RUN(test_server_lifecycle);
    RUN(test_server_port_default);
    RUN(test_http_html_page);
    RUN(test_http_mjpeg_stream);
    RUN(test_mdns_query_build);
    RUN(test_mdns_query_length);
    RUN(test_mdns_query_too_small);
    RUN(test_frame_push_ready);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed > 0 ? 1 : 0;
}
