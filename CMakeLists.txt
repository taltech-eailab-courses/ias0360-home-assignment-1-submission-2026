# Based on the IAS0360 Lab 1.1/1.2 Pico SDK targets; adapted for one C application.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH before configuring this project")
    endif()
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(imu_signal_analysis C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(icm20948)

add_executable(imu_signal_analysis
    main.c
    signal_processing.c
)

target_include_directories(imu_signal_analysis PRIVATE icm20948)
target_compile_options(icm20948 PRIVATE -Wall -Wextra)

target_compile_options(imu_signal_analysis PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(imu_signal_analysis
    pico_stdlib
    icm20948
    hardware_i2c
    m
)

pico_enable_stdio_usb(imu_signal_analysis 1)
pico_enable_stdio_uart(imu_signal_analysis 0)
pico_add_extra_outputs(imu_signal_analysis)
