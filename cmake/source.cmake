# ============================================================================
# Sources Organization
# ============================================================================
file(GLOB ARCH_SOURCES "arch/x86_64/*.cpp")
file(GLOB_RECURSE ARCH_FEATURE_SOURCES "arch/x86_64/features/*.cpp")
file(GLOB_RECURSE ARCH_GDT_SOURCES "arch/x86_64/gdt/*.cpp")
file(GLOB_RECURSE ARCH_DRIVER_SOURCES "arch/x86_64/drivers/*.cpp")
file(GLOB_RECURSE ARCH_IDT_SOURCES "arch/x86_64/idt/*.cpp")
file(GLOB_RECURSE ARCH_MM_SOURCES "arch/x86_64/mm/*.cpp")
file(GLOB_RECURSE ARCH_BOOT_SOURCES "arch/x86_64/boot/*.cpp")
file(GLOB_RECURSE ARCH_IRQ_SOURCES "arch/x86_64/irq/*.cpp")
file(GLOB_RECURSE ARCH_ENTRY_SOURCES "arch/x86_64/entry/*.cpp")
file(GLOB_RECURSE ARCH_LIB_SOURCES "arch/x86_64/lib/*.cpp")
file(GLOB_RECURSE ARCH_ASM_SOURCES "arch/x86_64/*.asm")
list(APPEND ARCH_SOURCES
    ${ARCH_FEATURE_SOURCES}
    ${ARCH_GDT_SOURCES}
    ${ARCH_DRIVER_SOURCES}
    ${ARCH_IDT_SOURCES}
    ${ARCH_MM_SOURCES}
    ${ARCH_IRQ_SOURCES}
    ${ARCH_BOOT_SOURCES}
    ${ARCH_ENTRY_SOURCES}
    ${ARCH_LIB_SOURCES}
    ${ARCH_ASM_SOURCES}
)
file(GLOB_RECURSE INIT_SOURCES "init/*.cpp")
file(GLOB KERNEL_SOURCES "ceryx/*.cpp")
file(GLOB_RECURSE KERNEL_BUILTIN_SOURCES "ceryx/builtin/*.cpp")
file(GLOB_RECURSE KERNEL_SCHED_SOURCES "ceryx/sched/*.cpp")
file(GLOB_RECURSE KERNEL_FKX_SOURCES "ceryx/fkx/*.cpp")
file(GLOB_RECURSE KERNEL_ASRX_SOURCES "ceryx/asrx/*.cpp")
file(GLOB KERNEL_SYSINTF_CORE_SOURCES "ceryx/sysintf/*.cpp")
file(GLOB_RECURSE KERNEL_ASM_SOURCES "ceryx/*.asm")
file(GLOB_RECURSE KERNEL_SYSINTF_SOURCES "ceryx/sysintf/core/*.cpp")
list(APPEND KERNEL_SOURCES
    ${KERNEL_SCHED_SOURCES}
    ${KERNEL_ASRX_SOURCES}
    ${KERNEL_FKX_SOURCES}
    ${KERNEL_SYSINTF_CORE_SOURCES}
    ${KERNEL_SYSINTF_SOURCES}
    ${KERNEL_BUILTIN_SOURCES}
    ${KERNEL_ASM_SOURCES}
)
file(GLOB DRIVER_SOURCES "drivers/*.cpp")
file(GLOB_RECURSE DRIVER_ACPI_SOURCES "drivers/acpi/*.cpp")
file(GLOB_RECURSE DRIVER_BUILTINS_SOURCES "drivers/builtins/*.cpp")
file(GLOB_RECURSE DRIVER_QEMU_SOURCES "drivers/qemu/*.cpp")
file(GLOB_RECURSE DRIVER_CHAR_SOURCES "drivers/char/*.cpp")
file(GLOB_RECURSE DRIVER_PCI_SOURCES "drivers/pci/*.cpp")
file(GLOB_RECURSE DRIVER_TIMER_SOURCES "drivers/timer/*.cpp")
file(GLOB_RECURSE DRIVER_DRM_SOURCES "drivers/graphics/drm/*.cpp")
list(APPEND DRIVER_SOURCES
    ${DRIVER_ACPI_SOURCES}
    ${DRIVER_BUILTINS_SOURCES}
    ${DRIVER_QEMU_SOURCES}
    ${DRIVER_DRM_SOURCES}
    ${DRIVER_TIMER_SOURCES}
    ${DRIVER_CHAR_SOURCES}
    ${DRIVER_PCI_SOURCES}
)
file(GLOB LIB_SOURCES "lib/*.cpp")
file(GLOB_RECURSE LIB_FONT_SOURCES "lib/fonts/*.cpp")
list(APPEND LIB_SOURCES
    ${LIB_FONT_SOURCES}
)
file(GLOB MM_SOURCES "mm/*.cpp")
file(GLOB_RECURSE MM_SAN_SOURCES "mm/san/*.cpp")
list(APPEND MM_SOURCES
    ${MM_SAN_SOURCES}
)
file(GLOB CRYPTO_SOURCES "crypto/*.cpp" "crypto/sha/*.cpp")
file(GLOB FS_SOURCES "fs/*.cpp")

# ============================================================================
# Build Include Directories
# ============================================================================
include_directories(SYSTEM
        .
        include
        include/lib
)

# ============================================================================
# Final Source List
# ============================================================================
set(CERYX_SOURCES
        ${INIT_SOURCES}
        ${KERNEL_SOURCES}
        ${LIB_SOURCES}
        ${DRIVER_SOURCES}
        ${MM_SOURCES}
        ${ARCH_SOURCES}
        ${CRYPTO_SOURCES}
        ${FS_SOURCES}
)
