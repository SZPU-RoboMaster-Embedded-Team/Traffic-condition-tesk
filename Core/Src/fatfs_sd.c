#include "fatfs_sd.h"
#include "main.h" // 引入底层硬件库

//  强行写死大疆 C 板的物理引脚 PB12
#define SD_CS_PORT GPIOB
#define SD_CS_PIN  GPIO_PIN_12

// SD卡各种极其晦涩的底层命令暗号
#define CMD0   (0)      /* GO_IDLE_STATE */
#define CMD1   (1)      /* SEND_OP_COND */
#define ACMD41 (0x80+41)/* SEND_OP_COND (SDC) */
#define CMD8   (8)      /* SEND_IF_COND */
#define CMD9   (9)      /* SEND_CSD */
#define CMD10  (10)     /* SEND_CID */
#define CMD12  (12)     /* STOP_TRANSMISSION */
#define CMD16  (16)     /* SET_BLOCKLEN */
#define CMD17  (17)     /* READ_SINGLE_BLOCK */
#define CMD18  (18)     /* READ_MULTIPLE_BLOCK */
#define CMD23  (23)     /* SET_BLOCK_COUNT */
#define ACMD23 (0x80+23)/*  补上漏掉的多块擦除指令宏 */
#define CMD24  (24)     /* WRITE_BLOCK */
#define CMD25  (25)     /* WRITE_MULTIPLE_BLOCK */
#define CMD55  (55)     /* APP_CMD */
#define CMD58  (58)     /* READ_OCR */

static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType;

// --- 底层物理收发小助手 ---
// 片选拉低（开始跟卡说话）
static void SELECT(void) {
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}
// 片选拉高（说完挂电话）
static void DESELECT(void) {
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
}

// 往 SPI 发送一个字节，并顺手读一个字节回来
static BYTE SPI_RxByte(void) {
    BYTE dummy, data;
    dummy = 0xFF;
    HAL_SPI_TransmitReceive(&hspi2, &dummy, &data, 1, 10);
    return data;
}

static void SPI_TxByte(BYTE data) {
    BYTE dummy;
    HAL_SPI_TransmitReceive(&hspi2, &data, &dummy, 1, 10);
}

// 等待 SD 卡准备好（不要超过 500 毫秒）
static BYTE SD_WaitReady(void) {
    BYTE res;
    uint32_t timeout = HAL_GetTick() + 500;
    SPI_RxByte(); // 消耗掉可能存在的垃圾数据
    do {
        res = SPI_RxByte();
    } while (res != 0xFF && HAL_GetTick() < timeout);
    return res;
}

// 向 SD 卡发送物理指令
static BYTE SD_SendCmd(BYTE cmd, DWORD arg) {
    BYTE n, res;
    if (cmd & 0x80) { // ACMD
        cmd &= 0x7F;
        res = SD_SendCmd(CMD55, 0);
        if (res > 1) return res;
    }

    DESELECT();
    SPI_RxByte();
    SELECT();
    if (SD_WaitReady() != 0xFF) return 0xFF;

    // 发送指令包 (6 字节)
    SPI_TxByte(cmd | 0x40);          // Start + Command
    SPI_TxByte((BYTE)(arg >> 24));   // Argument[31..24]
    SPI_TxByte((BYTE)(arg >> 16));   // Argument[23..16]
    SPI_TxByte((BYTE)(arg >> 8));    // Argument[15..8]
    SPI_TxByte((BYTE)arg);           // Argument[7..0]
    
    // CRC 校验码 (SPI 模式下只有 CMD0 和 CMD8 需要，其他随意)
    n = 0x01;
    if (cmd == CMD0) n = 0x95;       // CMD0 专属 CRC
    if (cmd == CMD8) n = 0x87;       // CMD8 专属 CRC
    SPI_TxByte(n);

    // 等待响应 (10 次尝试)
    if (cmd == CMD12) SPI_RxByte();  // CMD12 会多吐一个字节
    n = 10;
    do {
        res = SPI_RxByte();
    } while ((res & 0x80) && --n);

    return res;
}


// 🌟 暴漏给 user_diskio.c 的 5 个宏观魔法函数


// 1. 初始化硬盘
DSTATUS SD_disk_initialize(BYTE pdrv) {
    BYTE n, cmd, ty, ocr[4];
    uint32_t timeout;

    if (pdrv) return STA_NOINIT;

    // 刚上电，必须先温柔地发 80 个以上的时钟脉冲让它清醒
    DESELECT();
    for (n = 10; n; n--) SPI_RxByte();

    ty = 0;
    // 发送 CMD0，进入 SPI 模式
    if (SD_SendCmd(CMD0, 0) == 1) {
        timeout = HAL_GetTick() + 1000;
        if (SD_SendCmd(CMD8, 0x1AA) == 1) { // SDv2
            for (n = 0; n < 4; n++) ocr[n] = SPI_RxByte();
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                while (HAL_GetTick() < timeout && SD_SendCmd(ACMD41, 1UL << 30));
                if (HAL_GetTick() < timeout && SD_SendCmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = SPI_RxByte();
                    ty = (ocr[0] & 0x40) ? 3 : 2; // 判断是普通卡还是 HC 卡
                }
            }
        } else { // SDv1 or MMC
            cmd = (SD_SendCmd(ACMD41, 0) <= 1) ? ACMD41 : CMD1;
            while (HAL_GetTick() < timeout && SD_SendCmd(cmd, 0));
            if (HAL_GetTick() < timeout || SD_SendCmd(CMD16, 512) != 0) ty = 0;
        }
    }
    CardType = ty;
    DESELECT();
    SPI_RxByte();

    if (ty) {
        Stat &= ~STA_NOINIT; // 成功！
    } else {
        Stat |= STA_NOINIT;  // 失败！
    }
    return Stat;
}

// 2. 获取状态
DSTATUS SD_disk_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return Stat;
}

// 3. 读取扇区
DRESULT SD_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (!(CardType & 2)) sector *= 512; // 如果是老式卡，要把块地址换算成字节地址

    if (count == 1) { // 读单块
        if ((SD_SendCmd(CMD17, sector) == 0) && (SD_WaitReady() == 0xFF)) {
            while (SPI_RxByte() != 0xFE); // 等待数据头
            HAL_SPI_Receive(&hspi2, buff, 512, 100);
            SPI_RxByte(); SPI_RxByte();   // 丢弃 CRC
            count = 0;
        }
    } else { // 读多块
        if (SD_SendCmd(CMD18, sector) == 0) {
            do {
                while (SPI_RxByte() != 0xFE);
                HAL_SPI_Receive(&hspi2, buff, 512, 100);
                SPI_RxByte(); SPI_RxByte(); // 丢弃 CRC
                buff += 512;
            } while (--count);
            SD_SendCmd(CMD12, 0); // 发送停止指令
        }
    }
    DESELECT();
    SPI_RxByte();
    return count ? RES_ERROR : RES_OK;
}

// 4. 写入扇区
DRESULT SD_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (!(CardType & 2)) sector *= 512;

    if (count == 1) { // 写单块
        if (SD_SendCmd(CMD24, sector) == 0) {
            SPI_TxByte(0xFE); // 数据头
            HAL_SPI_Transmit(&hspi2, (BYTE*)buff, 512, 100);
            SPI_TxByte(0xFF); SPI_TxByte(0xFF); // 假 CRC
            if ((SPI_RxByte() & 0x1F) == 0x05) { // 检查是否被接受
                count = 0;
            }
        }
    } else { // 写多块
        if (CardType & 2) SD_SendCmd(ACMD23, count);
        if (SD_SendCmd(CMD25, sector) == 0) {
            do {
                SPI_TxByte(0xFC); // 多块数据头
                HAL_SPI_Transmit(&hspi2, (BYTE*)buff, 512, 100);
                SPI_TxByte(0xFF); SPI_TxByte(0xFF); // 假 CRC
                if ((SPI_RxByte() & 0x1F) != 0x05) break;
                buff += 512;
            } while (--count);
            SPI_TxByte(0xFD); // 停止传输标记
        }
    }
    SD_WaitReady();
    DESELECT();
    SPI_RxByte();
    return count ? RES_ERROR : RES_OK;
}

// 5. 获取硬盘信息 (IOCTL)
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    DRESULT res = RES_ERROR;
    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC: // 强制刷入数据
            SELECT();
            if (SD_WaitReady() == 0xFF) res = RES_OK;
            DESELECT();
            break;
        case GET_SECTOR_SIZE: // 物理扇区大小
            *(WORD*)buff = 512;
            res = RES_OK;
            break;
        case GET_BLOCK_SIZE: // 擦除块大小
            *(DWORD*)buff = 8;
            res = RES_OK;
            break;
        case GET_SECTOR_COUNT: // 容量计算 (这块较复杂，咱们平时记录数据不用到，简化处理)
            *(DWORD*)buff = 0; 
            res = RES_OK;
            break;
        default:
            res = RES_PARERR;
    }
    return res;
}