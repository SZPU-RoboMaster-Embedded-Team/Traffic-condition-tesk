#include "uart_hal.hpp"
#include "HI12_imu.hpp"

// 实例化 IMU 解析器
BSP::IMU::HI12_float my_imu;

// 定义接收缓冲区
#define RX_BUF_SIZE 256
uint8_t rx_buffer[RX_BUF_SIZE];

// 【调试专用】全局变量，方便在 Keil 的 Watch 窗口实时偷看！
float debug_z_acc = 0.0f;
float debug_pitch = 0.0f;
float debug_roll  = 0.0f;
float debug_yaw   = 0.0f;

// 暴露给 main.c 的纯 C 接口
extern "C" {

void App_Init(void) {
    // 1. 初始化 UART 总线
    HAL::UART::get_uart_bus_instance();
    
    // 2. 获取 USART1 设备 (因为你的传感器插在 4-pin 串口上)
    auto& uart1 = HAL::UART::get_uart_bus_instance().get_device(HAL::UART::UartDeviceId::HAL_Uart1);
    
    // 3. 将 Rx 接收区包装成库支持的 Data 结构体
    HAL::UART::Data rx_data{rx_buffer, RX_BUF_SIZE};
    
    // 4. 开启 DMA 循环接收
    uart1.receive_dma(rx_data);
}

void App_Loop(void) {
    // 每次循环，把缓冲区的数据喂给 IMU 解析器
    my_imu.DataUpdate(rx_buffer);
    
    // 提取出你需要的物理量，赋给全局变量供 Watch 窗口观察
    debug_z_acc = my_imu.GetAcc(2);     // Z轴加速度
    debug_pitch = my_imu.GetAngle(1);   // 俯仰角
    debug_roll  = my_imu.GetAngle(0);   // 横滚角
    debug_yaw   = my_imu.GetAngle(2);   // 航向角
}

} // extern "C"