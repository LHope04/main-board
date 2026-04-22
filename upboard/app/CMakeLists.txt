# Application firmware image, built once per OTA slot.
# Source identical between A and B — only the .ld and VECT_TAB_OFFSET differ,
# selected via UPBOARD_SLOT (set by the top-level CMakeLists from the preset).

if(NOT UPBOARD_SLOT MATCHES "^(A|B)$")
    message(FATAL_ERROR "UPBOARD_SLOT must be 'A' or 'B' (got '${UPBOARD_SLOT}').")
endif()

set(TARGET upboard_${UPBOARD_SLOT})
set(ELF   ${TARGET}.elf)

set(REPO_ROOT ${CMAKE_SOURCE_DIR})

if(UPBOARD_SLOT STREQUAL "A")
    set(LINKER_SCRIPT ${REPO_ROOT}/cmake/ld/STM32F407_APP_A.ld)
    set(VECT_TAB_OFFSET 0x00020000U)
else()
    set(LINKER_SCRIPT ${REPO_ROOT}/cmake/ld/STM32F407_APP_B.ld)
    set(VECT_TAB_OFFSET 0x00040000U)
endif()

set(STARTUP_FILE
    ${REPO_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s)

# HAL files used by the application. Adding files here costs flash and link
# time, so keep this list in sync with what the App actually needs
# (currently 20 HAL units).
set(HAL_SRC_DIR ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src)
set(HAL_SOURCES
    ${HAL_SRC_DIR}/stm32f4xx_hal.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_rcc.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_rcc_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_gpio.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_cortex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_pwr.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_pwr_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_dma.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_dma_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_uart.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_i2c.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_i2c_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_flash.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_flash_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_flash_ramfunc.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_exti.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_adc.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_adc_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_tim.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_tim_ex.c)

# Core/ holds CubeMX-generated wiring (clocks, MSP, HAL conf, vector table).
# app/ holds hand-written application modules (drivers, protocol, OTA, etc.).
# Common/ holds code shared with the bootloader (boot params + CRC32).
set(CORE_SRC_DIR ${REPO_ROOT}/Core/Src)
set(APP_SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/Src)
set(APP_SOURCES
    ${CORE_SRC_DIR}/main.c
    ${CORE_SRC_DIR}/gpio.c
    ${CORE_SRC_DIR}/i2c.c
    ${CORE_SRC_DIR}/usart.c
    ${CORE_SRC_DIR}/stm32f4xx_it.c
    ${CORE_SRC_DIR}/stm32f4xx_hal_msp.c
    ${CORE_SRC_DIR}/system_stm32f4xx.c
    ${APP_SRC_DIR}/ina226.c
    ${APP_SRC_DIR}/power_ctrl.c
    ${APP_SRC_DIR}/fan_ctrl.c
    ${APP_SRC_DIR}/compressor_ctrl.c
    ${APP_SRC_DIR}/sensor_acq.c
    ${APP_SRC_DIR}/buzzer.c
    ${APP_SRC_DIR}/esp_comm.c
    ${APP_SRC_DIR}/ota.c
    ${REPO_ROOT}/Common/boot_params.c
    ${REPO_ROOT}/Common/crc32.c)

add_executable(${ELF}
    ${STARTUP_FILE}
    ${APP_SOURCES}
    ${HAL_SOURCES})

# VECT_TAB_OFFSET is consumed by Core/Src/system_stm32f4xx.c to set SCB->VTOR
# at SystemInit() — must match the LMA the linker script places .isr_vector at.
target_compile_definitions(${ELF} PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    VECT_TAB_OFFSET=${VECT_TAB_OFFSET})

target_include_directories(${ELF} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${REPO_ROOT}/Core/Inc
    ${REPO_ROOT}/Common
    ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${REPO_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${REPO_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${REPO_ROOT}/Drivers/CMSIS/Include)

target_compile_options(${ELF} PRIVATE
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-Os -g>)

target_link_options(${ELF} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.map,--cref
    -Wl,--print-memory-usage)

set_target_properties(${ELF} PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT})

# .hex for ST-Link / OpenOCD program; .bin for OTA payload + CRC32 input.
add_custom_command(TARGET ${ELF} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${ELF}>
    COMMENT "Generating ${TARGET}.hex / ${TARGET}.bin and size report")

# Boot params blob — flashed once at 0x08008000 (sector 2) on a fresh board to
# point the bootloader at this slot. Regenerated whenever the App relinks so
# the file is always next to the .bin it pairs with.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(PARAMS_BIN ${CMAKE_CURRENT_BINARY_DIR}/params_${UPBOARD_SLOT}.bin)
    add_custom_command(TARGET ${ELF} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/make_params.py
                --slot ${UPBOARD_SLOT} -o ${PARAMS_BIN}
        BYPRODUCTS ${PARAMS_BIN}
        COMMENT "Generating params_${UPBOARD_SLOT}.bin (boot_slot=${UPBOARD_SLOT})")
else()
    message(WARNING "Python3 not found — params_${UPBOARD_SLOT}.bin will not be generated. "
                    "Run tools/make_params.py manually.")
endif()
