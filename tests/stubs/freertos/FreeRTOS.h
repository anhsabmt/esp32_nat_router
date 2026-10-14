#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffU
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
