cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generator if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
    # set(CMAKE_BUILD_TYPE "MinSizeRel")
    # set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME u073_gnd_header)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources (Core + HAL + ThreadX kernel + Azure RTOS)
add_subdirectory(cmake/stm32cubemx)

# ---------------------------------------------------------------------------
# Application-layer sources (from the original Keil project groups)
# ---------------------------------------------------------------------------

# handler module
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    handler/src/AT_handler.c
    handler/src/WAPI_M0804C.c
    handler/src/led_handler.c
    handler/src/stroage_handler.c
    handler/src/switch_handler.c
    handler/src/rfid_handler.c
)

# APP module
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    APP/src/led.c
    APP/src/upper_commu.c
    APP/src/wapi_commu.c
    APP/src/hall.c
    APP/src/rfid.c
    APP/src/low_power.c
    APP/src/voltage.c
)

# DEBUG / Segger RTT
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    DEBUG/SeggerRTT/Src/SEGGER_RTT.c
    DEBUG/SeggerRTT/Src/SEGGER_RTT_printf.c
)

# DEBUG / Debug console selectable backend (semihosting or RTT)
# Default backend is semihosting. Compile with -DDBG_CONSOLE_BACKEND_RTT
# to route output to SEGGER RTT instead.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    DEBUG/DebugConsole/Src/dbg_console.c
)

# 02_OS_Platform - OS_Wrapper
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    02_OS_Platform/OS_Wrapper/src/osal_heap.c
    02_OS_Platform/OS_Wrapper/src/osal_mutex.c
    02_OS_Platform/OS_Wrapper/src/osal_queue.c
    02_OS_Platform/OS_Wrapper/src/osal_sema.c
    02_OS_Platform/OS_Wrapper/src/osal_task.c
    02_OS_Platform/OS_Wrapper/src/osal_timer.c
)

# 02_OS_Platform - OS_Implementation (ThreadX backend)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_heap.c
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_mutex.c
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_queue.c
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_sema.c
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_task.c
    02_OS_Platform/OS_Implementation/ThreadX/src/os_impl_timer.c
)

# Middlewares - uart_proto
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/uart_proto/src/uart_proto.c
)

# Middlewares - agile_modbus
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/agile_modbus-master/src/agile_modbus.c
    Middlewares/agile_modbus-master/src/agile_modbus_rtu.c
    Middlewares/agile_modbus-master/util/agile_modbus_slave_util.c
)

# Utility - t_list
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Utility/t_list/Src/t_list.c
)

# Utility - algo_data_integrity
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Utility/Algo_data_integrity/Src/algo_data_integrity.c
)

# Protocol - high_reliable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    protocol/high_reliable/protocol_high_relia.c
)

# ---------------------------------------------------------------------------
# Application-layer include directories
# (Core/HAL/ThreadX includes are already provided by cmake/stm32cubemx)
# ---------------------------------------------------------------------------

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # handler module
    handler/inc
    # APP module
    APP/inc
    # DEBUG / Segger RTT
    DEBUG/SeggerRTT/Inc
    # DEBUG / Debug console
    DEBUG/DebugConsole/Inc
    # 02_OS_Platform
    02_OS_Platform/OS_Adapter/inc
    02_OS_Platform/OS_Implementation/ThreadX/inc
    02_OS_Platform/OS_Wrapper/inc
    # Utility
    Utility/t_list/Inc
    Utility/Algo_data_integrity/Inc
    # Middlewares
    Middlewares/uart_proto/inc
    Middlewares/agile_modbus-master/inc
    Middlewares/agile_modbus-master/util
    # prtocol
    protocol/high_reliable
)

# ---------------------------------------------------------------------------
# Link directories setup
# ---------------------------------------------------------------------------
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# ---------------------------------------------------------------------------
# Add project symbols (macros)
# (Core project defines come from cmake/stm32cubemx/MX_Defines_Syms)
# ---------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# ---------------------------------------------------------------------------
# Add linked libraries
# ---------------------------------------------------------------------------
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# create hex
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
)

# create bin
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -O3)