# ARM GCC bare-metal toolchain for STM32F407 (Cortex-M4F).
# Used by all targets (bootloader, app slot A, app slot B).

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# Skip the compiler identification link step — arm-none-eabi-gcc can't produce
# a hosted executable, so CMake's default try-compile fails without this.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

if(CMAKE_HOST_WIN32)
    set(TOOLCHAIN_EXT ".exe")
else()
    set(TOOLCHAIN_EXT "")
endif()

set(CMAKE_C_COMPILER    ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_EXT})
set(CMAKE_CXX_COMPILER  ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_EXT})
set(CMAKE_ASM_COMPILER  ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_EXT})
set(CMAKE_OBJCOPY       ${TOOLCHAIN_PREFIX}objcopy${TOOLCHAIN_EXT} CACHE FILEPATH "")
set(CMAKE_OBJDUMP       ${TOOLCHAIN_PREFIX}objdump${TOOLCHAIN_EXT} CACHE FILEPATH "")
set(CMAKE_SIZE          ${TOOLCHAIN_PREFIX}size${TOOLCHAIN_EXT}    CACHE FILEPATH "")

# Cortex-M4F: hard-float with FPv4-SP-D16.
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS} -ffunction-sections -fdata-sections -Wall -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

# --specs=nano.specs pulls in newlib-nano (small printf/malloc).
# --specs=nosys.specs stubs out _read/_write/_sbrk so we don't need a syscall
# implementation; code that calls printf without retargeting will just
# drop bytes into a nowhere-land, which matches our policy (no printf).
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CPU_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")
