cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)
project(home_assignment_1_digit_recognition C CXX ASM)
pico_sdk_init()

set(LAB_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/lib")
set(TFLM_DIR "${LAB_LIB_DIR}/pico-tflmicro")
if (NOT EXISTS "${TFLM_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "TFLite Micro library not found at ${TFLM_DIR}")
endif()
add_subdirectory(${TFLM_DIR} pico-tflmicro-build)
add_subdirectory(${LAB_LIB_DIR}/config pico-config-build)
add_subdirectory(${LAB_LIB_DIR}/font pico-font-build)

add_library(src src/model.cpp)
target_include_directories(src PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/models
  ${TFLM_DIR}
  ${TFLM_DIR}/src
)
target_link_libraries(src PUBLIC pico-tflmicro)

add_subdirectory(${LAB_LIB_DIR}/lcd pico-lcd-build)

add_executable(digit_recognition
  main.cpp
  models/mnist_model_data.cpp
)

target_include_directories(digit_recognition PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/models
  ${TFLM_DIR}
  ${TFLM_DIR}/src
  ${LAB_LIB_DIR}/config
  ${LAB_LIB_DIR}/font
  ${LAB_LIB_DIR}/lcd
)

target_link_libraries(digit_recognition
  src
  lcd
  font
  config
  pico_stdlib
  pico_multicore
  hardware_spi
  hardware_pwm
  pico-tflmicro
)

pico_enable_stdio_usb(digit_recognition 1)
pico_enable_stdio_uart(digit_recognition 0)
pico_add_extra_outputs(digit_recognition)
