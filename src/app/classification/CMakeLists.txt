cmake_minimum_required(VERSION 3.20)

# Phase 10A — Pixel-Based Classification GUI shell.
#
# Strong dependency on OpenCV (ml component required for Phase 10A.8 RF/SVM).
# Without OpenCV the static library is skipped entirely; the main app falls
# back to a disabled menu entry guarded by SICNU_HAS_CLASSIFY.
if(NOT SICNU_HAS_OPENCV)
    message(STATUS "Skipping qgis_app_classify — OpenCV ml component unavailable")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_library(qgis_app_classify STATIC
    qgsclassificationmainwindow.cpp
    rs_class_table_widget.cpp
    rs_class_quick_list.cpp
    rs_roi_tool_base.cpp
    rs_roi_tool_point.cpp
    rs_roi_tool_rectangle.cpp
    rs_roi_tool_polygon.cpp
    rs_roi_tool_freehand.cpp
    rs_spectral_curve_widget.cpp
)

target_include_directories(qgis_app_classify PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/gui/maptools
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(qgis_app_classify PUBLIC
    qgis_analysis
    qgis_core
    qgis_gui
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
)

target_compile_definitions(qgis_app_classify PUBLIC SICNU_HAS_OPENCV=1)

set_target_properties(qgis_app_classify PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

target_compile_features(qgis_app_classify PUBLIC cxx_std_17)
