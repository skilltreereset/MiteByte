#pragma once
#include <cstddef>
#include <cstdint>
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 3
struct esp_partition_t { size_t address, size; };
const esp_partition_t *esp_partition_find_first(int, int, const char *);
int esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
