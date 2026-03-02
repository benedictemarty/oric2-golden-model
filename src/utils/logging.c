/**
 * @file logging.c
 * @brief Logging system implementation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include "utils/logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static log_level_t current_log_level = LOG_LEVEL_INFO;

void log_init(log_level_t level) {
    current_log_level = level;
}

void log_cleanup(void) {
    /* Nothing to cleanup for now */
}

static void log_message(log_level_t level, const char* level_str, const char* format, va_list args) {
    if (level < current_log_level) {
        return;
    }

    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    if (!timeinfo) return;

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    printf("[%s] %s: ", time_str, level_str);
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
}

void log_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_INFO, "INFO", format, args);
    va_end(args);
}

void log_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_WARNING, "WARNING", format, args);
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_ERROR, "ERROR", format, args);
    va_end(args);
}
