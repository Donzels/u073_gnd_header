# AGENTS.md — WAPI 无线地线头端项目

> 本文件为 AI 编码助手提供项目背景、架构约定、编码规范和常见陷阱。

---

## 1. 项目概述

**WAPI 无线地线头端固件** — 基于 STM32U073 (Cortex-M0+, 128KB Flash, 40KB RAM) 的嵌入式系统，通过 WAPI (M0804C 模块) 实现无线通信，用于接地线挂接/拆除状态上报。

### 1.1 核心功能
- **霍尔传感器检测**：Hall1 长按进入 WAPI 配置模式；Hall2 边沿触发 RFID 读取 + WAPI 上报
- **RFID 标签读取**：125kHz 载波 + 曼彻斯特解码 (TIM2 PWM + TIM15 输入捕获)
- **WAPI 无线通信**：AT 命令状态机控制 M0804C 模块，支持证书/密码双认证方式
- **Modbus 配置**：无线 Modbus 从机模式，通过 Holding Register 配置网络参数和证书文件
- **低功耗管理**：ThreadX tickless + Stop 2 模式，RTC 定时唤醒（45分钟周期 + 24小时闹钟）
- **电压监测**：ADC1 采样 VREFINT 计算 VDDA，欠压触发休眠

### 1.2 硬件资源
| 资源 | 用途 |
|------|------|
| LPUART1 + DMA | WAPI 模块 AT 通信 |
| TIM2 CH3 | RFID 125kHz 载波 PWM |
| TIM15 CH1/CH2 | RFID 输入捕获 (上升/下降沿) |
| TIM7 | HAL 时基 (1ms tick) |
| ADC1 + DMA | 电压监测 (VREFINT) |
| RTC | 低功耗唤醒定时 |
| GPIO | LED(红/绿/蓝)、WAPI 电源/唤醒、RFID 电源、看门狗输出、系统电源开关 |

---

## 2. 架构分层

```
┌─────────────────────────────────────────────────────────────┐
│  APP Layer (应用层)                                          │
│  hall.c rfid.c wapi_commu.c upper_commu.c led.c              │
│  low_power.c voltage.c                                       │
├─────────────────────────────────────────────────────────────┤
│  Handler Layer (驱动/协议层)                                  │
│  WAPI_M0804C.c AT_handler.c switch_handler.c                 │
│  rfid_handler.c led_handler.c stroage_handler.c              │
├─────────────────────────────────────────────────────────────┤
│  Protocol / Middleware (协议/中间件)                          │
│  protocol_high_relia.c uart_proto.c agile_modbus.c           │
├─────────────────────────────────────────────────────────────┤
│  OSAL (操作系统抽象层)                                        │
│  OS_Wrapper → OS_Implementation/ThreadX                      │
├─────────────────────────────────────────────────────────────┤
│  RTOS / HAL / BSP                                            │
│  ThreadX 6.3.0 + STM32U0xx HAL + CMSIS                       │
└─────────────────────────────────────────────────────────────┘
```

### 2.1 关键设计模式
- **依赖注入**：所有 handler (WAPI、RFID、LED、Switch) 通过结构体注入 OS/HAL 接口，零硬编码 RTOS 依赖
- **表驱动**：AT 命令表、LED 控制表、WAPI 流程表均为数组 + 索引驱动
- **事件队列**：模块间通过 OSAL Queue 解耦 (hall2 边沿 → rfid 读取 → wapi 发送)
- **状态机**：WAPI M0804C 使用流程表实现 AT 命令状态机

---

## 3. 构建系统

### 3.1 工具链
- **编译器**: arm-none-eabi-gcc 10.3.1
- **构建**: CMake 3.22+ + Ninja
- **预设**: `Debug` (默认), `Release`

### 3.2 关键构建文件
| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 根项目：定义源文件、包含路径、编译选项 |
| `cmake/stm32cubemx/CMakeLists.txt` | CubeMX 生成：HAL + ThreadX 源码 + 启动文件 |
| `cmake/gcc-arm-none-eabi.cmake` | 工具链文件：含 `-Os` 等 Release 优化标志 |
| `STM32U073xx_FLASH.ld` | 链接脚本：Flash/RAM 布局，堆 0x1800，栈 0xC00 |

### 3.3 ⚠️ 关键编译选项陷阱（已修复）

**`-O3` 覆盖问题** (CMakeLists.txt:208):

原代码存在一个问题：根 `CMakeLists.txt` 第 208 行使用了无条件的 `-O3`：
```cmake
# ❌ 原错误代码（已修复）
# target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -O3)
```
这会导致**所有构建类型**（包括 Release）都强制使用 `-O3`，覆盖工具链为 Release 配置的 `-Os`，
使固件膨胀约 15-20KB。

**修复方式** — 改为仅 Debug 构建使用 `-O3`，Release 使用工具链默认 `-Os`：
```cmake
# ✅ 当前代码（已修复）
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-O3>)
```

对于 128KB Flash 的 MCU，Release 构建应始终使用 `-Os` 以优化尺寸。

### 3.4 构建命令
```bash
# Debug (默认)
cmake --preset Debug
cmake --build build/Debug

# Release (注意：需先修复 -O3 问题)
cmake --preset Release
cmake --build build/Release
```

### 3.5 实际构建尺寸数据

| 构建类型 | Flash (text+rodata) | RAM (bss+data) | 说明 |
|----------|---------------------|----------------|------|
| Debug    | ~101 KB             | ~31 KB         | 43 个 C 文件带 `-O3`，调试信息完整 |
| Release  | ~67 KB              | ~31 KB         | 全部 C 文件 `-Os`，无调试信息 |

> 注：Release 构建的当前 67KB 结果来自**已使用 `-Os` 的旧构建**。修复 `-O3` 条件化后重新构建，尺寸应保持一致或略降。

---

## 4. 编码规范

### 4.1 命名约定
| 类型 | 风格 | 示例 |
|------|------|------|
| 宏/常量 | 全大写 + 下划线 | `WAPI_RECV_BUF_SIZE`, `AT_CMD_END_MARKER` |
| 枚举 | 小写 + 下划线 | `wapi_status_t`, `switch_edge_t` |
| 结构体 | 小写 + 下划线 + `_t` | `m0804c_handler_t`, `at_input_arg_t` |
| 函数 | 小写 + 下划线 | `wapi_commu_init()`, `at_cmd_send_impl()` |
| 全局变量 | `g_` 前缀 | `g_wapi_handler_inst`, `g_hall2_edge_queue` |
| 静态局部 | 无特殊前缀 | `wapi_recv_buffer` |
| 函数指针 | `pf_` 前缀 | `pf_uart_write`, `pf_os_delay_ms` |
| 魔术数 | 大写 + 注释说明 | `WAPI_M0804C_MAGIC = 0x30383034` /* "0804" */ |

### 4.2 头文件组织
- 每个模块头文件包含完整 Doxygen 注释（@file @brief @param @retval）
- 公共 API 放在头文件；私有数据和前向声明用 `xxx_priv_data_t`
- 模块调试宏统一在头文件中定义，支持 RTT / semihosting / 静默 三态切换

### 4.3 错误处理
- 返回码使用枚举 (`wapi_status_t`, `at_status_t` 等)
- 魔术数校验：所有 handler 的 `inst()` 函数检查 `magic` 字段防止重复初始化
- 参数校验：NULL 指针检查是强制性的

### 4.4 中断安全
- ISR 中只调用以 `_isr_cb` 结尾的函数
- 禁止在 ISR 中使用 `printf` / `sprintf` / 阻塞操作
- DMA 环形缓冲区计算在 `notify_isr_cb()` 中完成，数据通过 Queue 投递到解析线程

---

## 5. 内存与尺寸

### 5.1 静态 RAM 分配 (总计 ~31KB)
| 分配项 | 大小 | 位置 |
|--------|------|------|
| OSAL Heap Pool | 20480 B | `os_impl_heap.c` (`OSAL_HEAP_POOL_SIZE`) |
| WAPI 接收缓冲区 | 2048 B | `main.c` (`WAPI_RECV_BUF_SIZE`) |
| ThreadX App Mem Pool | 2048 B | `app_azure_rtos_config.h` (`TX_APP_MEM_POOL_SIZE`) |
| SEGGER RTT 缓冲区 | 1024 B | `SEGGER_RTT_Conf.h` (`BUFFER_SIZE_UP`) |
| 各线程栈 (累计) | ~6KB | 多个 1024-2048 B 栈 |

### 5.2 Flash 尺寸优化建议（按收益排序）

1. **`-O3` → `-Os` 已修复**：CMakeLists.txt 已改为 `$<$<CONFIG:Debug>:-O3>`，Release 自动使用 `-Os`
2. **启用 ThreadX 最小化配置**：在 `Core/Inc/tx_user.h` 中取消注释以下宏，可减 2-4KB：
   - `TX_TIMER_PROCESS_IN_ISR` — 消除 timer 线程 TCB + 栈 + 上下文切换代码
   - `TX_DISABLE_REDUNDANT_CLEARING` — 跳过 .bss 已清零的全局变量重复清零
   - `TX_NOT_INTERRUPTABLE` — 减少临界区进出代码
   - `TX_NO_FILEX_POINTER` — 从 TCB 中移除 FileX 指针字段
3. **关闭调试输出**：Release 构建时让 `WAPI_DEBUG_OUT` / `AT_DEBUG_OUT` / `RFID_DEBUG_OUT` 等宏展开为空，可减 3-5KB 字符串+格式化代码
4. **精简 ThreadX 源码**：从 `cmake/stm32cubemx/CMakeLists.txt` 的 `ThreadX_Src` 列表中移除未使用的服务源文件：
   - 未使用 block pool → 移除 `tx_block_*.c`, `txe_block_*.c` (约 2KB)
   - 未使用 event flags → 移除 `tx_event_flags_*.c`, `txe_event_flags_*.c` (约 1.5KB)
   - 未使用 mutex（如只用 semaphore）→ 移除 `tx_mutex_*.c`, `txe_mutex_*.c` (约 2KB)
5. **移除未使用的 HAL 模块**：在 `cmake/stm32cubemx/CMakeLists.txt` 的 `STM32_Drivers_Src` 中，若某外设未使用可移除对应 `stm32u0xx_hal_xxx.c`（如未用 I2C/SPI 等）
6. **使用 `printf` 替代 `sprintf` 的替代方案**：`dbg_console_printf()` 已使用自研 `tiny_format`，不依赖 newlib-nano 的 `printf`，已较优。但如完全不需要格式化输出，可进一步将调试宏定义为空
7. **字符串去重**：检查 `.map` 中 `.rodata.str1.1` 段，确认编译器启用了字符串合并（GCC 默认 `-fmerge-constants`）
8. **LTO（链接时优化）**：在工具链文件中尝试添加 `-flto`，可减 5-10% 代码，但会增加链接时间并可能影响调试

---

## 6. 常见陷阱与注意事项

### 6.1 WAPI 通信
- **UART 接收缓冲区 2048B 是单缓冲**：DMA 循环模式，ISR 中计算 `CNDTR` 差值获取新数据范围
- **AT 命令发送需获取信号量**：`ACQUIRE_TRANS_SEND_SEMA` / `RELEASE_TRANS_SEND_SEMA` 保证串行化
- **透明发送回调在 ISR 上下文**：`at_trans_send()` 的回调可能从 UART TC 中断调用，勿阻塞

### 6.2 RFID 解码
- **输入捕获精度 1us**：TIM15 预分频到 1MHz，ARR 必须 > 384us (`HALF_PERIOD_HIGH`)
- **曼彻斯特解码容错**：`RFID_MORE_PRECISE=1` 使用双边沿捕获提高精度
- **读取超时自动重试**：`rfid_start_with_retry()` 最多重试 `RFID_READ_MAX_ATTEMPTS` 次

### 6.3 低功耗
- **Stop 2 下 LPUART1 无法唤醒**：LPUART1 时钟来自 PCLK1，Stop 2 会关闭。如需 UART 唤醒需改 clock source 为 LSE 或使用 Stop 1
- **RTC 唤醒定时器分辨率**：`CK_SPRE_16BITS` 模式约 0.49ms/计数，16 位最大约 32 秒。更长超时需分次进入
- **中断可抢占睡眠**：`low_power_schedule_sleep_ms()` 支持取消已有请求，新请求会重置延迟

### 6.4 调试输出
- **默认后端是 RTT**：`dbg_console.h` 中 `DBG_CONSOLE_BACKEND` 默认设为 `DBG_CONSOLE_BACKEND_RTT`
- **semihosting 需调试器连接**：若脱离调试器运行，semihosting 会导致 HardFault
- **tiny_format 不支持浮点**：`dbg_console_printf()` 的自定义 formatter 仅支持 `%d %u %x %X %s %c %p %o`

### 6.5 证书/配置存储
- **Flash 双分区**：`FLASH_0` (2 pages) 存证书文件，`FLASH_1` (1 page) 存用户配置
- **64-bit 双字写入**：`on_chip_flash_write()` 使用 `FLASH_TYPEPROGRAM_DOUBLEWORD`
- **完整性校验**：`algo_data_integrity.c` 提供 16-bit checksum (按字节累加取反)

---

## 7. 文件修改权限

| 文件/目录 | 说明 |
|-----------|------|
| `CMakeLists.txt` | 可修改：添加源文件、调整编译选项（**注意 `-O3` 陷阱**） |
| `cmake/stm32cubemx/CMakeLists.txt` | 谨慎修改：HAL/ThreadX 源文件列表，CubeMX 重新生成会覆盖 |
| `Core/Src/*`, `Core/Inc/*` | 谨慎修改：CubeMX 生成代码，USER CODE 区块内修改可保留 |
| `APP/src/*`, `handler/src/*` | 自由修改：应用层和驱动层代码 |
| `02_OS_Platform/*` | 可修改：OSAL 抽象层，但保持接口兼容性 |
| `Middlewares/*` | 谨慎修改：第三方库，建议通过配置宏调整行为 |

---

## 8. 测试与验证

- **构建验证**：修改后必须能成功构建 `Debug` 和 `Release` 预设
- **尺寸检查**：Release 构建后检查 `.bin`/`.map` 文件大小。当前 Release 约 **67KB** Flash，目标 < 64KB。优化路径参见 §5.2
- **RAM 检查**：关注 `.map` 文件中 `.bss` + `.data` 总和，当前约 **31KB**，确保 < 32KB (留 8KB 给栈和堆)。重点优化 `OSAL_HEAP_POOL_SIZE` (20KB) 和各线程栈深度
- **低功耗验证**：万用表测量 Stop 2 电流，目标 < 10uA

---

*Last updated: 2026-10-21*
