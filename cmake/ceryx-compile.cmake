# ============================================================================
# ceryx Linking & Compilation - Clang/LLVM
# ============================================================================

add_executable(ceryx.krnl
        ${CERYX_SOURCES}
)

target_link_libraries(ceryx.krnl PRIVATE FoundationKitMemory FoundationKitOsl FoundationKitCxxStl FoundationKitCxxAbi FoundationKitPlatform)

# ----------------------------------------------------------------------------
# base
# ----------------------------------------------------------------------------
target_compile_options(ceryx.krnl PRIVATE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:
        -m64
        -ffreestanding
        -nostdlib
        -fno-builtin
        -mno-red-zone

        -msoft-float
        -mno-80387

        -fPIE
        -fPIC
        -fno-plt
        -fvisibility=hidden
        -fdata-sections
        -ffunction-sections

        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
    >
    $<$<COMPILE_LANGUAGE:ASM_NASM>:
        -felf64
    >
)

target_compile_definitions(ceryx.krnl PUBLIC
    __ceryx__
)

if(COMPILER_IDENTIFIER STREQUAL "clang")
    target_compile_options(ceryx.krnl PRIVATE
        $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:
            -target ${CLANG_TARGET_TRIPLE}
            -mno-implicit-float
            -mcmodel=kernel
            -mno-retpoline
        >
    )
endif()

# ----------------------------------------------------------------------------
# optimizations
# ----------------------------------------------------------------------------
target_compile_options(ceryx.krnl PRIVATE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:
        -O${OPT_LEVEL}
        -g${DSYM_LEVEL}

        -fno-strict-aliasing
        -fno-common
        -fwrapv

        -Werror=implicit-function-declaration
        -Werror=incompatible-pointer-types
        -Werror=return-type
        -Werror=implicit-function-declaration
        -Werror=implicit-int
        -Werror=return-type
        -Werror=incompatible-pointer-types
        -Werror=int-conversion
        -Werror=format
        -Werror=format-security
        -Wstrict-prototypes
        -Wundef
        -Wdouble-promotion
        -Wframe-larger-than=2048
        -Wvla
        -Wshadow
    >
)

# ----------------------------------------------------------------------------
# linking
# ----------------------------------------------------------------------------
set(CERYX_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86_64/ceryx.ld" CACHE STRING "ceryx linker script")

set_target_properties(ceryx.krnl PROPERTIES LINK_DEPENDS "${CERYX_LINKER_SCRIPT}")

target_link_options(ceryx.krnl PRIVATE
    -T ${CERYX_LINKER_SCRIPT}
    -nostdlib
    -pie
    -Bsymbolic
    -melf_x86_64
    --gc-sections
    --icf=all
    -zrelro
    -znow
    -znoexecstack
    -zseparate-code
    --build-id=sha1
)

if (STRIP)
    add_custom_command(TARGET ceryx.krnl POST_BUILD
        COMMAND ${LLVM_STRIP} --strip-debug --strip-unneeded $<TARGET_FILE:ceryx.krnl>
        COMMENT "Stripping kernel image"
    )
endif()
