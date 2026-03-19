# ============================================================================
# Run Targets
# ============================================================================
if(BOCHS OR QEMU_SYSTEM_X86_64)
    set(ROM_IMAGE "/usr/share/OVMF/OVMF_CODE.fd" CACHE STRING "UEFI/Custom rom image")
endif()

if(QEMU_SYSTEM_X86_64)
    add_custom_target(run
            COMMAND ${QEMU_SYSTEM_X86_64}
            -M q35,hpet=on,kernel_irqchip=split
            -cpu max,+la57
            -accel kvm
            -no-reboot -no-shutdown
            -m 4G
            -smp sockets=2,cores=2
            -numa node,nodeid=0,cpus=0-1,memdev=mem0
            -numa node,nodeid=1,cpus=2-3,memdev=mem1
            -object memory-backend-ram,id=mem0,size=2G
            -object memory-backend-ram,id=mem1,size=2G
            -bios ${ROM_IMAGE}
            -debugcon file:bootstrap.log
            -serial stdio
            -boot d
            -device intel-iommu,intremap=on,caching-mode=on
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
