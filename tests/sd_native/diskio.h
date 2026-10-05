#pragma once
#include <stdint.h>
typedef uint8_t BYTE;
typedef int DRESULT;
#define RES_OK 0
#define CTRL_SYNC 0
DRESULT disk_read(BYTE, BYTE *, uint32_t, uint32_t);
DRESULT disk_write(BYTE, const BYTE *, uint32_t, uint32_t);
DRESULT disk_ioctl(BYTE, BYTE, void *);
