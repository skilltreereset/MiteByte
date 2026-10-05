#pragma once
#include <cstdint>
using TaskHandle_t = void *;
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelay(uint32_t);
