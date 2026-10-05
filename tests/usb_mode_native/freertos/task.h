#pragma once
#include <cstdint>
using TaskHandle_t = void *;
TaskHandle_t xTaskGetCurrentTaskHandle();
unsigned uxTaskPriorityGet(TaskHandle_t);
void vTaskPrioritySet(TaskHandle_t, unsigned);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
#define CONFIG_LWIP_TCPIP_TASK_PRIO 18
