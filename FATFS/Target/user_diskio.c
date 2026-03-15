#include <string.h>
#include "ff_gen_drv.h"
#include "user_diskio.h"
#include "fatfs_sd.h"  // 🌟 必须包含底层 SD 卡驱动头文件

/* 私有变量 */
static volatile DSTATUS Stat = STA_NOINIT;

/* 函数声明 */
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);

/* 🌟 这是 FatFs 寻找的核心结构体，也就是你在 .h 文件里 extern 的那个 */
Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
  USER_write,
  USER_ioctl,
};

/* 1. 初始化驱动 */
DSTATUS USER_initialize (BYTE pdrv)
{
  Stat = SD_disk_initialize(pdrv);
  return Stat;
}

/* 2. 获取磁盘状态 */
DSTATUS USER_status (BYTE pdrv)
{
  Stat = SD_disk_status(pdrv);
  return Stat;
}

/* 3. 读取扇区数据 */
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  return SD_disk_read(pdrv, buff, sector, count);
}

/* 4. 写入扇区数据 (必须开启 _USE_WRITE) */
DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  return SD_disk_write(pdrv, buff, sector, count);
}

/* 5. 磁盘控制 (获取大小等) */
DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
  return SD_disk_ioctl(pdrv, cmd, buff);
}