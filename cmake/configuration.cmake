# ============================================================================
# Build Configuration
# ============================================================================

# Defaults provided here, can be overridden by presets or -D
if(NOT DEFINED OPT_LEVEL)
    set(OPT_LEVEL "2" CACHE STRING "Optimization level (0, 1, 2, 3, s, z)")
endif()

if(NOT DEFINED DSYM_LEVEL)
    set(DSYM_LEVEL "0" CACHE STRING "Debug symbol level (0, 1, 2, 3)")
endif()

# Initrd/Initramfs
if(NOT DEFINED CERYX_INITRD)
    set(CERYX_INITRD "" CACHE FILEPATH "Path to the initramfs/initrd CPIO archive")
endif()
