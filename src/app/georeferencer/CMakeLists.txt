cmake_minimum_required(VERSION 3.20)

find_package(GDAL 3.4 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

add_library(qgis_app_georef STATIC
    qgsimagewarper.cpp
    qgsgeoreftransform.cpp
    qgsrasterchangecoords.cpp
)

target_include_directories(qgis_app_georef PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
    ${CMAKE_SOURCE_DIR}/src/core/raster
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(qgis_app_georef PUBLIC
    qgis_analysis
    qgis_core
    Qt6::Core
    Qt6::Gui
    GDAL::GDAL
)

set_target_properties(qgis_app_georef PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

target_compile_features(qgis_app_georef PUBLIC cxx_std_17)
