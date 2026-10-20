cmake_minimum_required(VERSION 3.25)

# The same tree builds two very different things:
#   * the firmware image, when cross-compiling with cmake/arm-none-eabi.cmake
#   * host-side builds (unit tests for hardware-independent code)
# Use the CMake presets rather than configuring by hand.

project(obd2_sniffer
    VERSION 0.4.0
    DESCRIPTION "CAN bus sniffer and OBD2 diagnostic tool for STM32F446RE"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # gnu11: HAL and CMSIS use GCC extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags for project code (vendor code is exempt).
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:C>:
        -Wall -Wextra -Werror
        -Wshadow -Wdouble-promotion -Wformat=2 -Wundef
        -Wstrict-prototypes -Wmissing-prototypes -Wcast-align>)

if(NOT CMAKE_CROSSCOMPILING)
    # Host builds: optional runtime sanitizers for the unit tests, e.g.
    # -DSANITIZE=address,undefined or -DSANITIZE=thread (see CMakePresets.json).
    set(SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for host builds")
    if(SANITIZE)
        # -fno-sanitize-recover: any finding aborts the test instead of
        # printing a warning and carrying on.
        add_compile_options(-fsanitize=${SANITIZE} -fno-sanitize-recover=all
                            -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${SANITIZE})
    endif()

    # Source-based coverage with MC/DC (Modified Condition/Decision Coverage),
    # Clang 18 or newer. scripts/coverage.py turns the results into reports.
    option(COVERAGE "Instrument host builds for line, branch and MC/DC coverage" OFF)
    if(COVERAGE)
        if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "COVERAGE=ON requires Clang (MC/DC needs Clang 18+)")
        endif()
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc)
        add_link_options(-fprofile-instr-generate)
    endif()
endif()

add_subdirectory(lib)

if(CMAKE_CROSSCOMPILING)
    enable_language(ASM)
    add_subdirectory(firmware)
else()
    enable_testing()
    add_subdirectory(tests)
endif()
