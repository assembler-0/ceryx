# ============================================================================
# Run Targets
# ============================================================================
if(BOCHS OR QEMU_SYSTEM_X86_64)
    set(ROM_IMAGE "/usr/share/OVMF/OVMF_CODE.fd" CACHE STRING "UEFI/Custom rom image")
endif()

if(QEMU_SYSTEM_X86_64)
    add_custom_target(run
            COMMAND ${QEMU_SYSTEM_X86_64}
            -no-reboot -no-shutdown
            -cpu max
            -m 4G
            -smp 2
            -debugcon file:bootstrap.log
            -serial stdio
            -boot d
            -cdrom ${ISO_PATH}
            DEPENDS iso
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running ceryx kernel in QEMU"
    )

    add_custom_target(run-bios
            COMMAND ${QEMU_SYSTEM_X86_64}
            -M q35,kernel_irqchip=split,hpet=on
            -cpu max
            -no-reboot -no-shutdown
            -m 2G
            -smp 4
            -debugcon file:bootstrap.log
            -serial stdio
            -boot d
            -device intel-iommu,intremap=on,caching-mode=on
            -cdrom ${ISO_PATH}
            DEPENDS iso
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running ceryx kernel in QEMU"
    )
endif()

if(LLVM_OBJDUMP)
    add_custom_target(dump
            COMMAND ${LLVM_OBJDUMP} -d $<TARGET_FILE:ceryx.krnl> > ceryx.dump
            COMMAND ${LLVM_OBJDUMP} -t $<TARGET_FILE:ceryx.krnl> > ceryx.sym
            DEPENDS ceryx.krnl
            COMMENT "Generating disassembly and symbols"
    )
endif()
