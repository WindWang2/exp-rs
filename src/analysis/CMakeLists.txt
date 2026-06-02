cmake_minimum_required(VERSION 3.20)

find_package(GDAL 3.4 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(qgis_analysis STATIC
    georeferencing/qgsgcppoint.cpp
    georeferencing/qgsgcptransformer.cpp
    georeferencing/qgsleastsquares.cpp
    georeferencing/qgsgcpgeometrytransformer.cpp
    georeferencing/qgsvectorwarper.cpp
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

target_compile_features(qgis_analysis PUBLIC cxx_std_17)
set_target_properties(qgis_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)
