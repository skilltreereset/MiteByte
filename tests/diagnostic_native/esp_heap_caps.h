#pragma once
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
inline unsigned heap_caps_get_free_size(unsigned) { return 32000; }
inline unsigned heap_caps_get_minimum_free_size(unsigned) { return 30000; }
