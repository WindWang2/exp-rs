cmake_minimum_required(VERSION 3.20)

find_package(GDAL 3.4 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(qgis_analysis STATIC
    georeferencing/qgsgcppoint.cpp
    georeferencing/qgsgcptransformer.cpp
    georeferencing/qgsleastsquares.cpp
    georeferencing/qgsrpcgcptransformer.cpp
)

target_include_directories(qgis_analysis PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/georeferencing
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
)

target_link_libraries(qgis_analysis PUBLIC
    qgis_core
    GDAL::GDAL
    Qt6::Core
)

# qgsleastsquares.cpp (helmert/projective) uses GSL LU/SVD.
if(HAVE_GSL)
    target_link_libraries(qgis_analysis PUBLIC GSL::gsl)
endif()

target_compile_features(qgis_analysis PUBLIC cxx_std_17)
set_target_properties(qgis_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Phase 10A — Pixel-based classification ROI model + I/O
add_subdirectory(classification)

# Phase 10B — OBIA segmentation data model + feature extraction
add_subdirectory(segmentation)

# ─── D13 · Radiometric Spectral Workbench ─────────────────────────────────
# 6S-style LUT atmospheric correction and hyperspectral continuum removal:
# pure analytic std-only kernels (headers carry no Qt), built C++20.
add_library(sicnu_spectral_analysis STATIC
    atmospheric/fast_6s_lookup.cpp
    hyperspectral/continuum_removal.cpp
)
target_include_directories(sicnu_spectral_analysis PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(sicnu_spectral_analysis PUBLIC cxx_std_20)
set_target_properties(sicnu_spectral_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)


