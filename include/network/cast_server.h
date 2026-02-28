/**
 * @file cast_server.h
 * @brief MJPEG Cast server + mDNS Chromecast discovery
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-28
 * @version 1.2.0-alpha
 *
 * HTTP MJPEG streaming server for casting the emulator display to a browser
 * or Chromecast device. Includes mDNS/DNS-SD discovery of Chromecast devices.
 *
 * Build with CAST=1 to enable (adds -DHAS_CAST -lpthread).
 */

#ifndef CAST_SERVER_H
#define CAST_SERVER_H

#include <stdint.h>
#include <stdbool.h>

/* Upscaled resolution: 240x224 * 3 = 720x672 */
#define CAST_UPSCALE_FACTOR  3
#define CAST_FRAME_W         (240 * CAST_UPSCALE_FACTOR)  /* 720 */
#define CAST_FRAME_H         (224 * CAST_UPSCALE_FACTOR)  /* 672 */
#define CAST_MAX_CLIENTS     8
#define CAST_DEFAULT_PORT    8080
#define CAST_JPEG_QUALITY    80

#ifdef HAS_CAST

#include <pthread.h>

typedef struct {
    uint16_t port;
    bool     active;

    /* Server thread */
    pthread_t thread;
    bool      thread_running;

    /* Shared framebuffer (upscaled RGB888) */
    uint8_t   upscaled[CAST_FRAME_W * CAST_FRAME_H * 3];
    pthread_mutex_t mutex;

    /* JPEG output buffer */
    uint8_t*  jpeg_buf;
    int       jpeg_len;
    int       jpeg_capacity;

    /* Client sockets */
    int       clients[CAST_MAX_CLIENTS];
    int       num_clients;

    /* Listen socket */
    int       listen_fd;

    /* Frame ready flag */
    bool      frame_ready;
} cast_server_t;

/**
 * @brief Initialize and start the MJPEG cast server
 * @param server Server instance
 * @param port TCP port to listen on (0 = default 8080)
 * @return true on success
 */
bool cast_server_init(cast_server_t* server, uint16_t port);

/**
 * @brief Push a new frame to the cast server
 *
 * Upscales the 240x224 RGB888 framebuffer to 720x672 using nearest-neighbor,
 * then signals the server thread that a new frame is available.
 *
 * @param server Server instance
 * @param framebuffer Source RGB888 framebuffer (240*224*3 bytes)
 * @param width Source width (240)
 * @param height Source height (224)
 */
void cast_server_push_frame(cast_server_t* server, const uint8_t* framebuffer,
                            int width, int height);

/**
 * @brief Stop and cleanup the cast server
 * @param server Server instance
 */
void cast_server_stop(cast_server_t* server);

/**
 * @brief Discover Chromecast devices on the network via mDNS/DNS-SD
 *
 * Sends a DNS-SD query for _googlecast._tcp.local and parses responses.
 * Prints discovered devices to stdout.
 *
 * @param timeout_ms Discovery timeout in milliseconds (0 = default 3000ms)
 * @return Number of devices found
 */
int cast_discover_devices(int timeout_ms);

/* Internal helpers (exposed for testing) */
void cast_upscale_nearest(const uint8_t* src, int src_w, int src_h,
                          uint8_t* dst, int factor);
int  cast_build_mdns_query(uint8_t* buf, int buf_size);

#else /* !HAS_CAST */

/* Stub structure when cast is not compiled in */
typedef struct {
    uint16_t port;
    bool     active;
} cast_server_t;

static inline bool cast_server_init(cast_server_t* s, uint16_t port) {
    (void)s; (void)port;
    return false;
}
static inline void cast_server_push_frame(cast_server_t* s,
                                          const uint8_t* fb,
                                          int w, int h) {
    (void)s; (void)fb; (void)w; (void)h;
}
static inline void cast_server_stop(cast_server_t* s) {
    (void)s;
}
static inline int cast_discover_devices(int timeout_ms) {
    (void)timeout_ms;
    return 0;
}

#endif /* HAS_CAST */

#endif /* CAST_SERVER_H */
