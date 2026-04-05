# ============================================================================
# Ceryx versioning system
# ============================================================================
execute_process(
        COMMAND git describe --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CERYX_RELEASE "0" CACHE STRING "Ceryx release")
set(CERYX_CANDIDATE "4" CACHE STRING "Ceryx release candidate")
set(CERYX_ABI_LEVEL "0" CACHE STRING "Ceryx ABI level")
set(CERYX_CODENAME "Integration" CACHE STRING "Ceryx codename")
