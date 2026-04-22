# Bootloader firmware image (FLASH 0x08000000, 32 KB).

set(TARGET bootloader)
set(ELF   ${TARGET}.elf)

set(REPO_ROOT ${CMAKE_SOURCE_DIR})
set(LINKER_SCRIPT ${REPO_ROOT}/cmake/ld/STM32F407_BOOTLOADER.ld)
set(STARTUP_FILE
    ${REPO_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s)

# HAL subset used by the bootloader. Trim aggressively — every file here
# lands in 32 KB of flash.
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
    ${HAL_SRC_DIR}/stm32f4xx_hal_flash.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_flash_ex.c
    ${HAL_SRC_DIR}/stm32f4xx_hal_exti.c)

add_executable(${ELF}
    ${STARTUP_FILE}
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${REPO_ROOT}/Core/Src/system_stm32f4xx.c
    ${REPO_ROOT}/Common/boot_params.c
    ${REPO_ROOT}/Common/crc32.c
    ${HAL_SOURCES})

target_compile_definitions(${ELF} PRIVATE
    USE_HAL_DRIVER
    STM32F407xx)

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

# Emit .hex (for ST-Link / OpenOCD program) and .bin (for OTA / CRC).
add_custom_command(TARGET ${ELF} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${ELF}>
    COMMENT "Generating ${TARGET}.hex / ${TARGET}.bin and size report")
