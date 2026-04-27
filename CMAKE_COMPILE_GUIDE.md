# STM32 音频可视化系统 - CMake编译配置指南

## 📝 CMakeLists.txt 配置

以下是需要对 `CMakeLists.txt` 进行的修改：

### 1. 添加ARM CMSIS库路径

在 `target_include_directories` 中添加：

```cmake
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 原有路径...
    Drivers/CMSIS/Include
    Drivers/CMSIS/DSP/Include
)
```

### 2. 链接ARM CMSIS库

在 `target_link_libraries` 中修改为：

```cmake
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    
    # 添加ARM DSP库 (选择合适版本)
    # 对于STM32F1, Cortex-M3，使用以下之一：
    -larm_cortexM3l      # 小库版本
    or
    -larm_cortexM3       # 标准版本
)
```

### 3. 完整参考示例

```cmake
cmake_minimum_required(VERSION 3.22)

# ... (原有配置)

# 创建可执行文件
add_executable(${CMAKE_PROJECT_NAME})

# 添加STM32CubeMX源码
add_subdirectory(cmake/stm32cubemx)

# 链接库
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 可选：CMSIS库搜索路径
    # Drivers/CMSIS/Lib/GCC/
)

# 添加源文件
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # 已经由add_subdirectory(cmake/stm32cubemx)包含:
    # - Core/Src/main.c
    # - Core/Src/bluetooth_uart.c
    # - Core/Src/audio_buffer.c
    # - Core/Src/fft_processor.c
    # - Core/Src/lcd_driver.c
    # - Core/Src/ui_renderer.c
    # - Core/Src/button_driver.c
)

# 添加包含目录
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32F1xx_HAL_Driver/Inc
    Drivers/CMSIS/Device/ST/STM32F1xx/Include
    Drivers/CMSIS/Include
    Drivers/CMSIS/DSP/Include
)

# 编译定义
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    STM32F103xB
    # 其他定义...
)

# 链接库
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    -larm_cortexM3l  # ARM DSP库
)
```

## 🔧 手动配置ARM CMSIS库

### 从GitHub下载ARM CMSIS库

```bash
# 克隆CMSIS仓库
git clone https://github.com/ARM-software/CMSIS_5.git

# 或下载ZIP:
# https://github.com/ARM-software/CMSIS_5/releases
```

### 将库文件复制到项目

```
项目结构应为:
STM32CodeDemo/
├── Drivers/
│   ├── CMSIS/
│   │   ├── Core/
│   │   ├── DSP/
│   │   │   ├── Include/      ← DSP头文件
│   │   │   └── Lib/          ← ARM DSP库文件
│   │   ├── Device/
│   │   └── Include/          ← 软件接口
│   └── STM32F1xx_HAL_Driver/
├── Core/
│   ├── Inc/
│   └── Src/
└── CMakeLists.txt
```

### 选择正确的库文件

对于STM32F103C8T6 (Cortex-M3)，使用以下库之一：

```
Drivers/CMSIS/DSP/Lib/GCC/
├── libarm_cortexM3l_math.a    ← 小库版本（推荐）
├── libarm_cortexM3_math.a     ← 标准版本
└── libarm_cortexM3b_math.a    ← Big-endian版本
```

推荐使用 `libarm_cortexM3l_math.a` (小库，代码量最小)

## 🏗️ 编译步骤

### 使用STM32CubeIDE

```
1. File → Open Projects from File System
2. 选择项目根目录
3. 项目自动配置CMake
4. Project → Build Project
   或按 Ctrl+B
```

### 使用命令行编译

```bash
# 创建构建目录
mkdir build
cd build

# 配置CMake
cmake -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=../cmake/stm32f103.cmake ..

# 编译
make -j4

# 输出文件位置
# build/STM32CodeDemo.elf
# build/STM32CodeDemo.hex
```

## 📦 输出的二进制文件

编译成功后，生成的文件：

```
build/
├── STM32CodeDemo.elf      ← ELF格式 (用于调试)
├── STM32CodeDemo.hex      ← 十六进制 (用于烧录)
├── STM32CodeDemo.bin      ← 二进制 (用于烧录或比较)
└── CMakeFiles/            ← 临时文件
```

## 🔗 库依赖关系

```
main.c
  ├─► bluetooth_uart.c    (不依赖DSP库)
  ├─► audio_buffer.c      (不依赖DSP库)
  ├─► fft_processor.c     (☆ 依赖: arm_rfft_fast_f32, arm_cmplx_mag_f32)
  ├─► lcd_driver.c        (不依赖DSP库)
  ├─► ui_renderer.c       (不依赖DSP库)
  └─► button_driver.c     (不依赖DSP库)
  
ARM DSP库 (libarm_cortexM3l_math.a)
  ├─ arm_rfft_fast_f32()     ← FFT处理器使用
  └─ arm_cmplx_mag_f32()     ← FFT幅度计算
```

## ⚠️ 常见编译错误

### 错误1: "undefined reference to `arm_rfft_fast_f32'"

```
原因: 未链接ARM DSP库
解决: 
1. 确认CMakeLists.txt中有 -larm_cortexM3l
2. 确认库文件路径正确
3. 检查target_link_directories配置
```

### 错误2: "fatal error: arm_math.h: No such file"

```
原因: ARM CMSIS头文件路径未添加
解决:
1. 在target_include_directories中添加 Drivers/CMSIS/DSP/Include
2. 检查文件夹是否真实存在
```

### 错误3: "undefined reference to `HAL_UART_Receive_IT'"

```
原因: STM32 HAL库未完整配置
解决:
1. 确保cmake/stm32cubemx/CMakeLists.txt包含HAL源文件
2. 在CubeMX中重新生成代码
```

## 📊 编译输出大小

预期的最终二进制大小：

```
STM32CodeDemo.elf: ~200-300 KB
STM32CodeDemo.hex: ~100-150 KB (实际烧录大小)
Flash占用: ~60-80 KB (含ARM DSP库)
RAM占用: ~15-18 KB
```

STM32F103C8T6规格：
```
Flash: 64 KB  ← ⚠️ 空间偏紧！
RAM:   20 KB
```

### 优化编译大小

如果编译后超过64KB，可以：

```cmake
# CMakeLists.txt中修改优化级别
add_compile_options(-Os)  # Size优化 (而非-O2速度优化)
```

---

**按以上步骤配置，项目应该能正常编译！** ✅
