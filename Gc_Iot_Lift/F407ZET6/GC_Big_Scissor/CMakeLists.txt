cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
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
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Ball_And_Plant_System)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add user application sources
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    APP/Src/app_spi.c
    APP/Src/app_io_map.c
    APP/Src/app_maintenance.c
    APP/Src/app_op_log.c
    APP/Src/app_product.c
    APP/Src/app_rise_counter.c
    APP/Src/app_rise_queue.c
    APP/Src/app_tas_dtu.c
    APP/Src/app_w25qxx.c
    APP/Src/lift_core.c
    APP/Src/lift_iot.c
    APP/Src/lift_large_scissor.c
    APP/Src/lift_lock.c
    BSP/Src/bsp_spi.c
    BSP/Src/bsp_w25qxx.c
    BSP/Src/bsp_uart.c
    Middlewares/FIFO/fifo.c
    RTT_easylog/easylogger/src/elog.c
    RTT_easylog/easylogger/src/elog_async.c
    RTT_easylog/easylogger/src/elog_buf.c
    RTT_easylog/easylogger/src/elog_utils.c
    RTT_easylog/easylogger/port/elog_port.c
    RTT_easylog/easylogger/plugins/file/elog_file.c
    RTT_easylog/easylogger/plugins/file/elog_file_port.c
    RTT_easylog/RTT/SEGGER_RTT.c
    RTT_easylog/RTT/SEGGER_RTT_printf.c
    Driver/Src/dri_motor.c
    Driver/Src/dri_safety.c
    Driver/Src/dri_key.c
    Driver/Src/dri_lift.c
    Driver/Src/dri_tas_dtu.c
    APP/Src/motor.c
    APP/Src/encoder.c
    APP/Src/balance.c
    APP/Src/safety.c
    APP/Src/key.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    APP/Inc
    BSP/Inc
    Middlewares/FIFO
    Core/Inc
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    RTT_easylog/easylogger/inc
    RTT_easylog/easylogger/port
    RTT_easylog/easylogger/plugins/file
    RTT_easylog/easylogger/plugins/flash
    RTT_easylog/RTT
    Driver/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# -----------------------------------------------------------
# Post-build: generate .hex and .bin files
# -----------------------------------------------------------
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating hex and bin files"
)

# -----------------------------------------------------------
# PyOCD flash target
# Prerequisite: pyocd pack install STM32F407VETx
# -----------------------------------------------------------
add_custom_target(flash
    COMMAND pyocd flash -t stm32f407zetx --erase auto $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    DEPENDS ${CMAKE_PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Flashing ${CMAKE_PROJECT_NAME} via PyOCD (requires: pyocd pack install STM32F407ZETx)"
    USES_TERMINAL
)

add_custom_target(debug
    COMMAND pyocd gdbserver -t stm32f407zetx
    DEPENDS ${CMAKE_PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Starting PyOCD GDB server"
    USES_TERMINAL
)
