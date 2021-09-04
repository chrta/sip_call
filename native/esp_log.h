#pragma once

#include <cstdarg>
#include <cstdio>

static inline void my_log(const char* level, const char* prefix, const char* fmt, ...)
{
    printf("[%s] ", level);
    printf("[%s] ", prefix);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

#define ESP_LOGE(prefix, fmt, ...) my_log("ERR", prefix, fmt, ##__VA_ARGS__)
#define ESP_LOGW(prefix, fmt, ...) my_log("WARN", prefix, fmt, ##__VA_ARGS__)
#define ESP_LOGI(prefix, fmt, ...) my_log("INFO", prefix, fmt, ##__VA_ARGS__)
#define ESP_LOGD(prefix, fmt, ...) my_log("DBG", prefix, fmt, ##__VA_ARGS__)
#define ESP_LOGV(prefix, fmt, ...) my_log("VER", prefix, fmt, ##__VA_ARGS__)
