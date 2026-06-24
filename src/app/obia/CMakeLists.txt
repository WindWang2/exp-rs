cmake_minimum_required(VERSION 3.20)

# Phase 10B — OBIA (Object-Based Image Analysis) classification.
# Requires OpenCV for classifier backends.
if(NOT SICNU_HAS_OPENCV)
    message(STATUS "Skipping qgis_app_obia — OpenCV ml component unavailable")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_library(qgis_app_obia STATIC
    rs_obia_task.cpp
    rs_segment_select_tool.cpp
    rs_segment_info_dock.cpp
    rs_obia_main_window.cpp
)

target_include_directories(qgis_app_obia PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/gui/maptools
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(qgis_app_obia PUBLIC
    qgis_analysis
    qgis_core
    qgis_gui
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
    GDAL::GDAL
)

target_compile_definitions(qgis_app_obia PUBLIC SICNU_HAS_OPENCV=1)

set_target_properties(qgis_app_obia PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

target_compile_features(qgis_app_obia PUBLIC cxx_std_17)
