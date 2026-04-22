cmake_minimum_required(VERSION 3.22)

# Top-level project. Individual firmware images (bootloader, app slot A,
# app slot B) live in subdirectories and are selected via CMakePresets.
# Use presets so the toolchain file and target are always consistent.
project(upboard C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Per-target subdirs are added based on a cache variable set by the preset.
# Keeping them opt-in means `cmake --preset bootloader` builds *only* the
# bootloader ELF, no stale App artifacts in the build tree.
set(UPBOARD_TARGET "" CACHE STRING "Which firmware image to build: bootloader | app-a | app-b")

if(UPBOARD_TARGET STREQUAL "bootloader")
    add_subdirectory(bootloader)
elseif(UPBOARD_TARGET STREQUAL "app-a")
    set(UPBOARD_SLOT "A")
    add_subdirectory(app)
elseif(UPBOARD_TARGET STREQUAL "app-b")
    set(UPBOARD_SLOT "B")
    add_subdirectory(app)
else()
    message(FATAL_ERROR
        "UPBOARD_TARGET not set. Use `cmake --preset {bootloader|app-a|app-b}` (see CMakePresets.json).")
endif()
