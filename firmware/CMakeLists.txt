# Firmware image for the STM32F446RE (Nucleo-64).

set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(HAL_DIR     ${THIRD_PARTY}/stm32f4xx-hal-driver)
set(DEVICE_DIR  ${THIRD_PARTY}/cmsis-device-f4)
set(CORE_DIR    ${THIRD_PARTY}/cmsis-core)

if(NOT EXISTS ${HAL_DIR}/Inc/stm32f4xx_hal.h)
    message(FATAL_ERROR
        "ST HAL sources are missing. Run: git submodule update --init --recursive")
endif()

# -Og keeps code debuggable while still small and fast; -g3 adds macro info.
set(CMAKE_C_FLAGS_DEBUG        "-Og -g3")
set(CMAKE_C_FLAGS_MINSIZEREL   "-Os -g3 -DNDEBUG")
set(CMAKE_ASM_FLAGS_DEBUG      "-g3")
set(CMAKE_ASM_FLAGS_MINSIZEREL "-g3")

# ---------------------------------------------------------------------------
# Device definitions shared by everything that includes the HAL/CMSIS headers.
# ---------------------------------------------------------------------------
add_library(stm32_device INTERFACE)
target_compile_definitions(stm32_device INTERFACE
    STM32F446xx
    USE_HAL_DRIVER
    HSE_VALUE=8000000U)
# SYSTEM: vendor headers don't trip our stricter warning flags.
target_include_directories(stm32_device SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # stm32f4xx_hal_conf.h
    ${HAL_DIR}/Inc
    ${DEVICE_DIR}/Include
    ${CORE_DIR}/Include)

# ---------------------------------------------------------------------------
# ST HAL, built as a static library with relaxed warnings (it is vendor code).
# Only the modules enabled in stm32f4xx_hal_conf.h are compiled.
# ---------------------------------------------------------------------------
set(HAL_MODULES
    hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_exti
    hal_dma hal_dma_ex hal_flash hal_flash_ex hal_flash_ramfunc
    hal_pwr hal_pwr_ex hal_can hal_iwdg hal_rtc hal_rtc_ex
    hal_tim hal_tim_ex hal_uart)
list(TRANSFORM HAL_MODULES PREPEND ${HAL_DIR}/Src/stm32f4xx_)
list(TRANSFORM HAL_MODULES APPEND .c)

add_library(stm32_hal STATIC
    ${HAL_MODULES}
    ${DEVICE_DIR}/Source/Templates/system_stm32f4xx.c)
target_link_libraries(stm32_hal PUBLIC stm32_device)
target_compile_options(stm32_hal PRIVATE -w)

# ---------------------------------------------------------------------------
# Build version: regenerated on every build from `git describe`, but the
# header is only rewritten when the version actually changes.
# ---------------------------------------------------------------------------
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_target(version_header
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in
        -DOUTPUT=${GENERATED_DIR}/version.h
        -DFALLBACK_VERSION=${PROJECT_VERSION}
        -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${GENERATED_DIR}/version.h
    COMMENT "Checking firmware version")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(FW_NAME obd2_sniffer)
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F446RETx_FLASH.ld)

add_executable(${FW_NAME}
    ${DEVICE_DIR}/Source/Templates/gcc/startup_stm32f446xx.s
    src/main.c
    src/board.c
    src/can_drv.c
    src/can_gen.c
    src/can_stream.c
    src/clock.c
    src/commands.c
    src/console.c
    src/fault.c
    src/fault_entry.S
    src/obd.c
    src/reset_cause.c
    src/status_led.c
    src/timebase.c
    src/uart_link.c
    src/watchdog.c
    src/stm32f4xx_it.c
    src/stm32f4xx_hal_msp.c
    src/syscalls.c)
set_target_properties(${FW_NAME} PROPERTIES SUFFIX .elf)
add_dependencies(${FW_NAME} version_header)

target_include_directories(${FW_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GENERATED_DIR})
# Our own code is held to a stricter standard than the vendor code.
target_link_libraries(${FW_NAME} PRIVATE stm32_hal core project_warnings)

target_link_options(${FW_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${FW_NAME}.map,--cref)
set_property(TARGET ${FW_NAME} APPEND PROPERTY LINK_DEPENDS ${LINKER_SCRIPT})

# .bin/.hex for flashing tools and release artifacts, plus a size report.
add_custom_command(TARGET ${FW_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${FW_NAME}> ${FW_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${FW_NAME}> ${FW_NAME}.hex
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${FW_NAME}>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    BYPRODUCTS ${FW_NAME}.bin ${FW_NAME}.hex
    VERBATIM)

# ---------------------------------------------------------------------------
# `cmake --build --preset firmware --target flash`: program over the Nucleo's
# on-board ST-LINK with OpenOCD, verify, and reset into the new image.
# ---------------------------------------------------------------------------
find_program(OPENOCD openocd)
if(OPENOCD)
    add_custom_target(flash
        COMMAND ${OPENOCD} -f board/st_nucleo_f4.cfg
                -c "program $<TARGET_FILE:${FW_NAME}> verify reset exit"
        DEPENDS ${FW_NAME}
        USES_TERMINAL
        VERBATIM)
else()
    message(STATUS "openocd not found: `flash` target disabled")
endif()
