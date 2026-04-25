cmake_minimum_required(VERSION 3.30)
project(ceryx-userspace LANGUAGES C ASM_NASM)

set(CMAKE_C_STANDARD 23)

add_executable(init.elf
    crt0.asm
    syscalls.asm
    init.c
)

# Only apply freestanding flags to C files
set_source_files_properties(init.c PROPERTIES COMPILE_FLAGS "-ffreestanding -nostdlib -fno-stack-protector -m64")

target_link_options(init.elf PRIVATE
    -nostdlib
    -static
    -T ${CMAKE_CURRENT_SOURCE_DIR}/init.ld
)

# We want the output to be named 'init' in the final CPIO
set_target_properties(init.elf PROPERTIES OUTPUT_NAME "init")
