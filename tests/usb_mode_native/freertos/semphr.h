#pragma once
#include <freertos/FreeRTOS.h>
using SemaphoreHandle_t = bool *;
SemaphoreHandle_t xSemaphoreCreateBinary();
void xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, unsigned);
void vSemaphoreDelete(SemaphoreHandle_t);
