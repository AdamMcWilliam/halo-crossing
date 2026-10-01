# Included by the host's pc/CMakeLists.txt when -DHC_ROOT=<repo> is given
# (see patches/host/). Adds the Halo Crossing layer to the ac_pc target.

file(GLOB HC_SOURCES CONFIGURE_DEPENDS
    ${HC_ROOT}/src/halo/*.c
    ${HC_ROOT}/src/integration/*.c
    ${HC_ROOT}/src/prototype/*.c
)

# Host call sites are guarded by this define, so it must reach every TU.
add_compile_definitions(HALO_CROSSING)

include_directories(
    ${HC_ROOT}/src
    ${HC_ROOT}/src/integration
)

set_source_files_properties(${HC_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wno-unused-parameter;-Wno-missing-field-initializers;-Wno-unused-function;-Wno-missing-braces")

list(LENGTH HC_SOURCES HC_SOURCE_COUNT)
message(STATUS "Halo Crossing layer: ${HC_ROOT} (${HC_SOURCE_COUNT} sources)")
