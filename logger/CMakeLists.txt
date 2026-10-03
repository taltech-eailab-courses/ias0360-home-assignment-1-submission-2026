# IMU logger: records the ICM-20948 to the SD card as 16-byte binary records.
# This is the Lab 1.1 project with the output file name, run length and start
# delay lifted into build options, so one recording needs no source edit:
#
#   cmake -DOUT_NAME=walking -DRUN_SECONDS=15 -DSTART_DELAY_MS=10000 ..
#
# The board support libraries are shared with the analysis program one level
# up rather than copied, which is why the add_subdirectory calls carry an
# explicit binary directory.

cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(logger C CXX ASM)
pico_sdk_init()

add_subdirectory(icm20948)
add_subdirectory(../lib/config       lib_config)
add_subdirectory(../lib/lcd          lib_lcd)
add_subdirectory(../lib/font         lib_font)
add_subdirectory(../lib/sd_card_driver sd_driver)

add_executable(logger
    main.c
    ../config/hw_config.c
)

set(IMU_VARIANT 7 CACHE STRING "Logger variant (6 = single core, 7 = two core)")
set(OUT_NAME "imu" CACHE STRING "Base name of the .bin file written to SD")
set(RUN_SECONDS 15 CACHE STRING "Length of one capture run, seconds")
set(START_DELAY_MS 3000 CACHE STRING "Delay before logging starts, ms")
target_compile_definitions(logger PRIVATE
    IMU_VARIANT=${IMU_VARIANT}
    OUT_NAME="${OUT_NAME}"
    RUN_SECONDS=${RUN_SECONDS}
    START_DELAY_MS=${START_DELAY_MS})

include_directories(./icm20948 ../lib/config ../lib/lcd ../lib/font)

target_link_libraries(logger
    pico_stdlib icm20948 hardware_i2c pico_multicore
    lcd font config sd_custom_driver
    pico_util hardware_clocks hardware_adc hardware_pio
)

pico_enable_stdio_usb(logger 1)
pico_enable_stdio_uart(logger 0)
pico_add_extra_outputs(logger)
