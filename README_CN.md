# STM32F103 音频频谱可视化系统 - 快速开始指南

## 📋 项目概述

这是一个基于STM32F103C8T6的音频频谱实时显示系统，支持：
- ✅ 蓝牙音频接收 (HC-06模块)
- ✅ FFT频谱分析 (2048点)
- ✅ ILI9341 LCD屏显示
- ✅ 按键切换时域/频域显示

## 🔧 环境配置

### 必需工具
```
1. STM32CubeMX 6.x+
2. STM32CubeIDE 或 Keil MDK
3. arm-gcc-none-eabi 工具链
4. ARM CMSIS-DSP 库
```

### 安装步骤

**Step 1: 配置STM32CubeMX**

1. 打开 `STM32CodeDemo.ioc` 文件
2. 添加以下外设配置：

```
系统时钟配置:
  - HSE: 8MHz 外部晶振 (如果有)
  - PLL: ×9 = 72MHz 系统时钟
  - HCLK: 72MHz
  - APB1: 36MHz
  - APB2: 72MHz

UART1 (蓝牙通信):
  - 波特率: 9600
  - 数据位: 8
  - 停止位: 1
  - 奇偶位: None
  - DMA: UART1_RX (可选，用于提高实时性)
  - 中断: USART1_IRQHandler (enable)

SPI1 (LCD驱动):
  - 模式: Master
  - 方向: 2线全双工
  - 数据宽度: 8位
  - 时钟频率: 36MHz
  - NSS: Software controlled

GPIO配置:
  - PA9:  UART1_TX (LCD驱动)
  - PA10: UART1_RX (LCD驱动)
  - PA5:  SPI1_SCK (LCD驱动)
  - PA7:  SPI1_MOSI (LCD驱动)
  - PA3:  GPIO_Output (LCD DC)
  - PA4:  GPIO_Output (LCD CS)
  - PB0:  GPIO_Input (KEY_MODE)
  - PB1:  GPIO_Input (KEY_SETTING)
  - PB10: GPIO_Input (KEY_HELP)

中断优先级:
  - UART1: 优先级 1
  - SysTick: 优先级 15
```

**Step 2: 生成代码**

在STM32CubeMX中点击 "Generate Code"

**Step 3: 获取ARM CMSIS-DSP库**

```bash
# 下载ARM CMSIS库
# https://github.com/ARM-software/CMSIS_5

# 解压后将 DSP库放到项目中:
Drivers/CMSIS/DSP/
```

## 📝 文件结构

```
Core/
├── Inc/
│   ├── config.h           # 全局配置
│   ├── bluetooth_uart.h   # 蓝牙驱动
│   ├── audio_buffer.h     # 音频缓冲
│   ├── fft_processor.h    # FFT处理
│   ├── lcd_driver.h       # LCD驱动
│   ├── ui_renderer.h      # UI渲染
│   └── button_driver.h    # 按键驱动
│
└── Src/
    ├── main.c              # 主程序
    ├── bluetooth_uart.c    # 蓝牙驱动实现
    ├── audio_buffer.c      # 音频缓冲实现
    ├── fft_processor.c     # FFT处理实现
    ├── lcd_driver.c        # LCD驱动实现
    ├── ui_renderer.c       # UI渲染实现
    └── button_driver.c     # 按键驱动实现
```

## 🔌 硬件接线

### UART1 (蓝牙模块)

```
HC-06模块         STM32
GND       ───────► GND
VCC(5V)   ───────► USB 5V
TX        ───────► PA10 (RX)
RX        ───┬────► PA9 (TX)
             │
             分压: 5V → 3.3V
             [2.2kΩ] ──┬──► HC-06 RX
                       │
                      [4.7kΩ]
                       │
                      GND
```

### SPI1 (ILI9341 LCD屏)

```
ILI9341         STM32
GND     ───────► GND
VCC(3.3V)──────► 3.3V
CS      ───────► PA4
DC      ───────► PA3
MOSI    ───────► PA7 (SPI1)
SCK     ───────► PA5 (SPI1)
MISO    ───────► PA6 (可选)
RST     ───────► 3.3V (直接连)
```

### 按键接线

```
按键1 (模式切换)
  一端 ──────► PB0
  另一端 ──────► GND

按键2
  一端 ──────► PB1
  另一端 ──────► GND

按键3
  一端 ──────► PB10
  另一端 ──────► GND

PB的GPIO已配置上拉，按下时为低电平
```

## 💾 编译过程

### 使用STM32CubeIDE

```bash
1. File → Open Projects from File System
2. 选择项目文件夹
3. 构建 (Project → Build Project)
4. 如果编译错误，检查：
   - ARM CMSIS库路径是否正确
   - 包含路径是否添加到CMakeLists.txt
```

### 编译配置修改

编辑 `CMakeLists.txt`，添加CMSIS-DSP库：

```cmake
# 在 target_link_libraries 中添加:
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    -larm_cortexM3_mathlib  # ARM DSP库
)

# 在 target_include_directories 中添加:
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/CMSIS/DSP/Include
)
```

## 🚀 烧录程序

### 使用ST-Link调试器

```
1. 连接ST-Link V2到STM32开发板
2. 在STM32CubeIDE中: Run → Debug
3. 或: Run → Run As → STM32 C/C++ Application
```

### 使用USB-TTL烧录 (备选)

```
需要引导程序支持（本项目未集成）
```

## 🔍 调试步骤

### Step 1: 检查蓝牙通信

```
使用串口调试工具测试UART1:
- 波特率: 9600
- 数据位: 8
- 停止位: 1
- 奇偶位: None

预期: 接收蓝牙模块发送的数据
```

### Step 2: 检查LCD显示

```
LCD初始化后，屏幕应该：
1. 背光点亮 (白屏或有光)
2. 收到FFT数据后显示波形或频谱

如果屏幕无反应:
- 检查SPI通信是否正常
- 检查CS和DC引脚电平
- 检查引脚是否接触良好
```

### Step 3: 检查按键

```
按下PB0按键，应该：
1. 在时域和频域之间切换显示
2. 如果未响应，检查按键接线和去抖设置
```

### Step 4: 完整系统测试

```
1. 打开手机蓝牙
2. 搜索 "HC-06" 配对
3. 播放音乐或使用蓝牙音乐软件
4. LCD屏幕应显示实时波形
5. 按下按键0切换到频谱模式
6. 看到频谱柱状图
```

## 🐛 常见问题

**Q1: 编译错误 "undefined reference to `arm_rfft_fast_f32`"**

A: 需要链接ARM CMSIS-DSP库
```cmake
# CMakeLists.txt中添加
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    -larm_cortexM3L  # 或其他合适的库版本
)
```

**Q2: LCD屏显示垃圾数据或不动**

A: 
- 检查SPI连接（特别是SCK和MOSI）
- 确认3.3V电源正常
- 尝试调整SPI时钟分频 (BaudRatePrescaler)
- 检查ILI9341的初始化命令是否完整

**Q3: 蓝牙无法接收数据**

A:
- 检查UART1接线
- 验证波特率是否9600
- 检查HC-06的电源和GND连接
- 尝试用串口助手验证接收

**Q4: 按键无反应**

A:
- 检查PB0/PB1/PB10是否接GND
- 确保上拉电阻生效 (GPIO_PULLUP已配置)
- 增加去抖延时 (DEBOUNCE_TIME_MS)

## 📊 性能指标

```
采样率:        16kHz
FFT大小:       2048点
频率分辨率:    7.8Hz (16000/2048)
时间分辨率:    128ms (2048/16000)
LCD刷新率:     约20Hz (取决于UART接收速度)
CPU占用:       约40-60%
```

## 🔄 后续扩展

一旦第一版本运行正常，可以扩展：

1. **LED频谱显示**
   - 添加RGB LED条
   - 按频率显示能量分布

2. **语音识别**
   - 集成MFCC特征提取
   - 实现DTW匹配算法

3. **更多UI模式**
   - 频谱瀑布图
   - 相位分析
   - 教学参数显示

## 📞 调试支持

如遇到问题，请提供：
1. 编译错误信息（完整）
2. 烧录后观察到的现象
3. 接线照片
4. 使用的硬件型号

---

**祝编译烧录顺利！** 🎉
