#pragma once
#include <cstdint>
#define portMAX_DELAY UINT32_MAX
#define CONFIG_IDF_TARGET_ESP32S3 1
using BaseType_t = int;
using TickType_t = uint32_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
