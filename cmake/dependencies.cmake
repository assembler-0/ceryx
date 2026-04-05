# ============================================================================
# Tool Dependencies & External Packages
# ============================================================================
include(FetchContent)

# ----------------------------------------------------------------------------
# Required Tools
# ----------------------------------------------------------------------------

# Assembler (NASM)
find_program(NASM_EXECUTABLE nasm REQUIRED)

# ISO Creation (Xorriso)
find_program(XORRISO xorriso REQUIRED)

if (STRIP)
    find_program(LLVM_STRIP llvm-strip REQUIRED)
endif()

# ----------------------------------------------------------------------------
# Optional Tools (Emulation / Debugging)
# ----------------------------------------------------------------------------
find_program(QEMU_SYSTEM_X86_64 qemu-system-x86_64)
find_program(LLVM_OBJDUMP llvm-objdump)
