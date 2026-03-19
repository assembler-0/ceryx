# ============================================================================
# SPDX-License-Identifier: Apache-2.0 
#
# ceryx (R) kernel
#
# @file CMakeLists.txt
# @brief Kernel build configuration
# @copyright (C) 2025-2026 assembler-0
# ============================================================================

# ============================================================================
# Project & Toolchain Setup
# ============================================================================

cmake_minimum_required(VERSION 3.30)
project(ceryx
    VERSION 5.4.9
    LANGUAGES C ASM_NASM
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/toolchain")

# ============================================================================
# Configuration & Dependencies
# ============================================================================

# 1. Base Configuration
include(configuration)  # Build options
include(version)        # Versioning info
include(platform)       # Platform checks
include(standard)       # C/C++ Standards

# 2. Dependencies
include(dependencies)   # External tools (NASM, Xorriso, etc.)
include(limine)         # Bootloader resources

# ============================================================================
# Kernel Build
# ============================================================================

# Source Gathering
include(source)       # Core kernel sources

# Kernel Target Definition & Compilation Rules
include(ceryx-compile)

# ============================================================================
# Post-Build & artifacts
# ============================================================================
include(isogen) # ISO Generation
include(run)    # Run targets (qemu, etc.)
