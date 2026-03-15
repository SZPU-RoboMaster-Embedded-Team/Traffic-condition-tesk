#include "uart_hal.hpp"
#include "HI12_imu.hpp"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "ff.h"      // 🌟 文件系统头文件
#include <stdio.h>  
#include <stdlib.h>  // 用于 abs 函数

volatile int sd_err_code = 0;
// 实例化 IMU 解析器
BSP::IMU::HI12_float my_imu;

// 定义接收缓冲区
#define RX_BUF_SIZE 256
uint8_t rx_buffer[RX_BUF_SIZE];
uint8_t parse_buffer[82];

// 实时物理量
float debug_acc_x = 0.0f, debug_acc_y = 0.0f, debug_acc_z = 0.0f;

extern "C" {

    // 状态显示水桶
    char csv_line[64];
    volatile bool is_recording = false;

    // SD卡管理对象
    FATFS fs;           // 工作区
    FIL file;           // 文件对象
    char filename[20];  // 动态文件名
    volatile bool sd_check_pass = false; // SD卡体检标志

    void App_Init(void) {
        // 1. 硬件起床延时
        HAL_Delay(1000); 

        // 2. 🌟 物理强起 PB12 (CS引脚)
        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); // 默认挂断状态

        // 3. 传感器初始化 (DMA模式)
        HAL::UART::get_uart_bus_instance();
        auto& uart1 = HAL::UART::get_uart_bus_instance().get_device(HAL::UART::UartDeviceId::HAL_Uart1);
        volatile uint32_t tmpreg = USART1->SR;
        tmpreg = USART1->DR; (void)tmpreg;
        HAL::UART::Data rx_data{ rx_buffer, RX_BUF_SIZE };
        uart1.receive_dma(rx_data);

        // 4. 🌟 SD卡开机体检 (引入智能重试机制，专治冷启动)
        FRESULT res;
        int retry_count = 0;
        const int MAX_RETRIES = 3; // 最大允许重试 5 次

        // 只要没成功，且重试次数没用完，就一直试
        while (retry_count < MAX_RETRIES) {
            res = f_mount(&fs, "", 1);
            sd_err_code = res; // 记录最新的错误码

            if (res == FR_OK) {
                // 挂载成功！
                sd_check_pass = true;
                sprintf(csv_line, "SD Ready! Press BTN to start.");
                f_mount(NULL, "", 0); // 测完先安全卸载，保持清洁
                break; // 成功了就立刻跳出循环，不再重试
            } else {
                // 挂载失败，说明 SD 卡的电容还没充满，或者芯片还没睡醒
                retry_count++;
                HAL_Delay(100); // 🌟 核心护城河：强制等 300 毫秒！让电容飞一会儿！
            }
        }

        // 如果循环结束了，重试次数达到了上限，说明是真的坏了或者没插卡
        if (retry_count >= MAX_RETRIES) {
            sd_check_pass = false;
        }
    }

    void App_Loop(void) {
        uint32_t current_time_ms = HAL_GetTick();

        // ================= 🌟 第零块：LED 灯语状态机 🌟 =================
        static uint32_t last_led_tick = 0; // LED 专属时间戳

        if (!sd_check_pass) {
            // 🚨 状态 3：致命错误！红灯疯狂快闪 (100ms翻转一次)，绿灯死灭
            HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
            if (current_time_ms - last_led_tick > 100) {
                HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
                last_led_tick = current_time_ms;
            }
        }
        else if (is_recording) {
            // 🟩 状态 2：正在录制！绿灯常亮，红灯死灭
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
        }
        else {
            // 🟢 状态 1：待命心跳！绿灯缓慢呼吸闪烁 (500ms翻转一次)，红灯死灭
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
            if (current_time_ms - last_led_tick > 500) {
                HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);
                last_led_tick = current_time_ms;
            }
        }

        // ================= 🌟 第一块：按键边沿检测逻辑 🌟 =================
        static uint32_t last_button_press_time = 0;
        static uint8_t last_button_state = GPIO_PIN_SET;
        uint8_t current_button_state = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9);

        // 检测按下瞬间 (下降沿)
        if (last_button_state == GPIO_PIN_SET && current_button_state == GPIO_PIN_RESET) {
            if (current_time_ms - last_button_press_time > 50) {
                
                // 只有体检通过才允许录制
                if (sd_check_pass) {
                    is_recording = !is_recording; // 翻转录制状态

                    if (is_recording) {
                        // 【启动录制逻辑】
                        f_mount(&fs, "", 1);
                        
                        // 自动寻名: data_1.csv, data_2.csv...
                        int file_index = 1;
                        FILINFO fno;
                        while(1) {
                            sprintf(filename, "data_%d.csv", file_index);
                            if (f_stat(filename, &fno) != FR_OK) break; // 找不到这个文件，说明可以用这个名
                            file_index++;
                        }
                        
                        f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
                        f_puts("Time_ms,Acc_X,Acc_Y,Acc_Z\n", &file); // 写表头
                    } else {
                        // 【停止录制逻辑】
                        f_close(&file);
                        f_mount(NULL, "", 0); // 安全卸载
                    }
                }
                last_button_press_time = current_time_ms;
            }
        }
        last_button_state = current_button_state;

        // ================= 🌟 第二块：100Hz 数据处理与写入 🌟 =================
        static uint32_t last_record_time = 0;
        static int locked_header_index = -1;

        if (current_time_ms - last_record_time >= 10) {
            last_record_time = current_time_ms;

            // 数据提取逻辑
            if (locked_header_index != -1) {
                for (int i = 0; i < 82; i++) parse_buffer[i] = rx_buffer[(locked_header_index + i) % RX_BUF_SIZE];
                my_imu.DataUpdate(parse_buffer);
                rx_buffer[locked_header_index] = 0x00;
                locked_header_index = -1;
            }

            for (int i = 0; i < RX_BUF_SIZE; i++) {
                if (rx_buffer[i] == 0x5A && rx_buffer[(i + 1) % RX_BUF_SIZE] == 0xA5) {
                    locked_header_index = i;
                    break;
                }
            }

            // 更新物理量
            debug_acc_x = my_imu.GetAcc(0);
            debug_acc_y = my_imu.GetAcc(1);
            debug_acc_z = my_imu.GetAcc(2);

            // 🌟【核心写入动作】🌟
            if (is_recording && sd_check_pass) {
                // 拼接并存入 CSV
                sprintf(csv_line, "%u,%.3f,%.3f,%.3f\n", current_time_ms, debug_acc_x, debug_acc_y, debug_acc_z);
                f_puts(csv_line, &file); 
            } else if (!sd_check_pass) {
               sprintf(csv_line, "SD Error Code: %d", sd_err_code);
            } else {
                sprintf(csv_line, "Standby. Press PE9.");
            }
        }
    }
} // extern "C"