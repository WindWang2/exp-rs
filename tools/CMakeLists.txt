# Lab Sample Data Foundry (goal D1) — deterministic sample-data generator for
# docs/labs. Qt-free by design: only GDAL and jsoncpp (both REQUIRED at the
# root), mirroring the src/geospatial leaf policy, so this target configures
# and builds on every lane (Ninja/MSVC/Make, configure_wb7.cmd) without
# pulling the QGIS stack, and ships as a standalone binary (D7 bundle).
#
# FP contraction is disabled for the synth kernels: the foundry's
# determinism contract (tools/sample_foundry.h) relies on exactly-rounded
# IEEE-754 arithmetic; -ffp-contract=fast (GCC default at -O1+) would fuse
# mul+add into FMA and change bytes across compilers.

add_library(sicnu_sample_foundry STATIC
    sample_foundry.cpp
    sha256.cpp
)
target_link_libraries(sicnu_sample_foundry PUBLIC GDAL::GDAL jsoncpp)
target_include_directories(sicnu_sample_foundry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(sicnu_sample_foundry PRIVATE /fp:precise)
else()
    target_compile_options(sicnu_sample_foundry PRIVATE -ffp-contract=off -fno-fast-math)
endif()

add_executable(sicnu_generate_samples generate_sample_data.cpp)
target_link_libraries(sicnu_generate_samples PRIVATE sicnu_sample_foundry)
