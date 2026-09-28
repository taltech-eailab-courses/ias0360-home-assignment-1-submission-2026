# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
include(pico_sdk_import.cmake)
project(pico-filters)
pico_sdk_init()
add_subdirectory(../lab_1_1/imu_example/icm20948 ${CMAKE_BINARY_DIR}/icm20948)
add_subdirectory(../lab_1_1/lcd_sd_card_example/lib/sd_card_driver ${CMAKE_BINARY_DIR}/sd_card_driver)
include_directories(../lab_1_1/imu_example/icm20948)
add_executable(main
# enable one file with main() at a time
# одновременно включаем только один файл с функцией main
fft_imu.c
# fft output is also written to the sd card
# результат fft также записывается на sd-карту
hw_config.c
#statistics_imu.c
#quantization_imu.c
#quantization.c
#fft.c
#sobel.c
#statistic.c
)
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

pico_add_extra_outputs(main)

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(main pico_stdlib icm20948 sd_custom_driver hardware_i2c m)
