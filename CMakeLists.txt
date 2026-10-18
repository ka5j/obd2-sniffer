cmake_minimum_required(VERSION 3.25)

# The same tree builds two very different things:
#   * the firmware image, when cross-compiling with cmake/arm-none-eabi.cmake
#   * host-side builds (unit tests for hardware-independent code)
# Use the CMake presets rather than configuring by hand.

project(obd2_sniffer
    VERSION 0.1.0
    DESCRIPTION "CAN bus sniffer and OBD2 diagnostic tool for STM32F446RE"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # gnu11: HAL and CMSIS use GCC extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CROSSCOMPILING)
    enable_language(ASM)
    add_subdirectory(firmware)
else()
    message(STATUS "Host build: no host targets defined")
endif()
