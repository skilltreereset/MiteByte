#pragma once
#include "FreeRTOS.h"
struct FakeSemaphore;
using SemaphoreHandle_t = FakeSemaphore *;
SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateMutex();
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
