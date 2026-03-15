#ifndef __FATFS_SD_H
#define __FATFS_SD_H

#include "integer.h"
#include "diskio.h"
#include "stm32f4xx_hal.h" // 🌟 大疆 C 板是 F4 系列芯片，完美匹配

// 声明我们在 CubeMX 里配置好的 SPI2 句柄
extern SPI_HandleTypeDef hspi2; 

// 暴露给 user_diskio.c 调用的 5 个底层物理函数
DSTATUS SD_disk_initialize (BYTE pdrv);
DSTATUS SD_disk_status (BYTE pdrv);
DRESULT SD_disk_read (BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_write (BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl (BYTE pdrv, BYTE cmd, void* buff);

#endif