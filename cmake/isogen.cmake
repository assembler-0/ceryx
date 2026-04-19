# ============================================================================
# ISO Generation with Limine Bootloader
# ============================================================================

add_custom_command(
        OUTPUT initrd.cpio
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/initrd_root/sbin
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_CURRENT_BINARY_DIR}/initrd_root/sbin/init
        COMMAND sh -c "cd ${CMAKE_CURRENT_BINARY_DIR}/initrd_root && find . | cpio -o -H newc > ${CMAKE_CURRENT_BINARY_DIR}/initrd.cpio"
        DEPENDS init.elf
        COMMENT "Generating initrd.cpio..."
        VERBATIM
)

add_custom_command(
        OUTPUT bootd
        COMMAND ${CMAKE_COMMAND} -E touch bootd
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/bootdir/ceryx
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/bootdir/module
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/bootdir/EFI/BOOT
        # Copy Limine UEFI bootloader
        COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_RESOURCE_DIR}/BOOTX64.EFI
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/EFI/BOOT/BOOTX64.EFI
        # Copy kernel
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ceryx.krnl>
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/ceryx/ceryx.krnl
        # Copy Initrd
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/initrd.cpio
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/module/initrd.cpio
        # Copy Limine config
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/scripts/limine.conf
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/limine.conf
        DEPENDS ceryx.krnl initrd.cpio ${CMAKE_CURRENT_SOURCE_DIR}/scripts/limine.conf
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Setting up boot directory with Limine"
        VERBATIM
)

add_custom_target(bootdir ALL DEPENDS bootd)

add_custom_command(
        OUTPUT ceryx.hybrid.iso
        # 1. Copy UEFI boot binary
        COMMAND ${CMAKE_COMMAND} -E copy
        ${LIMINE_RESOURCE_DIR}/limine-uefi-cd.bin
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/limine-uefi-cd.bin

        # 2. Copy BIOS boot binary for hybrid support
        COMMAND ${CMAKE_COMMAND} -E copy
        ${LIMINE_RESOURCE_DIR}/limine-bios-cd.bin
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/limine-bios-cd.bin

        COMMAND ${CMAKE_COMMAND} -E copy
        ${LIMINE_RESOURCE_DIR}/limine-bios.sys
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir/limine-bios.sys

        # 3. Create the hybrid ISO with both BIOS and UEFI support
        COMMAND ${XORRISO} -as mkisofs
        -b limine-bios-cd.bin           # BIOS boot (El Torito)
        -no-emul-boot                   # No emulation for BIOS
        -boot-load-size 4               # Load 4 sectors
        -boot-info-table                # Create boot info table for BIOS
        --efi-boot limine-uefi-cd.bin   # UEFI boot
        --efi-boot-part                 # EFI boot partition
        --efi-boot-image                # Mark as EFI bootable
        --protective-msdos-label        # Hybrid MBR
        -o ${CMAKE_CURRENT_BINARY_DIR}/ceryx.hybrid.iso
        ${CMAKE_CURRENT_BINARY_DIR}/bootdir

        DEPENDS bootd $<TARGET_FILE:ceryx.krnl>
        ${LIMINE_RESOURCE_DIR}/limine-uefi-cd.bin
        ${LIMINE_RESOURCE_DIR}/limine-bios.sys
        ${LIMINE_RESOURCE_DIR}/limine-bios-cd.bin
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating hybrid ISO (ceryx.hybrid.iso)..."
        VERBATIM
)

set(ISO_PATH ${CMAKE_CURRENT_BINARY_DIR}/ceryx.hybrid.iso)
add_custom_target(iso ALL DEPENDS ceryx.hybrid.iso)
