cmake_minimum_required(VERSION 3.20)

find_package(GDAL 3.4 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

add_library(qgis_app_georef STATIC
    qgsimagewarper.cpp
    qgsgeoreftransform.cpp
    qgsrasterchangecoords.cpp
    qgsgcplist.cpp
    qgsgcplistmodel.cpp
    qgsgcplistwidget.cpp
    qgsgeoref_shell_window.cpp
    qgsgeoreferencermainwindow.cpp
    qgsgeoref_image_to_map_window.cpp
    rs_georef_mode_toggle.cpp
    rs_twincanvas_sync_controller.cpp
    qgsgeorefdatapoint.cpp
    qgsgcpcanvasitem.cpp
    qgsresidualplotitem.cpp
    qgsgeorefdelegates.cpp
    qgsgeoreftooladdpoint.cpp
    qgsgeoreftooldeletepoint.cpp
    qgsgeoreftoolmovepoint.cpp
    qgsmapcoordsdialog.cpp
    qgsgeorefvalidators.cpp
    rs_rms_scatter_widget.cpp
    rs_georef_params_panel.cpp
    rs_warp_task.cpp
    rs_sift_matcher.cpp
    rs_sift_dialog.cpp
    rs_sift_task.cpp
    rs_template_matcher.cpp
    rs_template_match_dialog.cpp
    rs_georef_session_state.cpp
    rs_georeferencing_session.cpp
    rs_georef_task_center_executor.cpp
    rs_georef_task_list.cpp
    rs_georef_workflow_bridge.cpp
    ../dialogs/dialog_help_catalog.cpp
    ../dialogs/dialog_utils.cpp
)

target_include_directories(qgis_app_georef PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
    ${CMAKE_SOURCE_DIR}/src/core/raster
    ${CMAKE_SOURCE_DIR}/src/core/settings
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/gui/proj
    ${CMAKE_SOURCE_DIR}/src/gui/maptools
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
    ${CMAKE_SOURCE_DIR}/src/ui/georeferencer
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(qgis_app_georef PUBLIC
    qgis_analysis
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_workflow
    sicnu_jobs
    sicnu_task_center
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    GDAL::GDAL
)

set_target_properties(qgis_app_georef PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/src/ui/georeferencer"
)

target_compile_features(qgis_app_georef PUBLIC cxx_std_17)

# OpenCV — OPTIONAL (SIFT + template match). Without it, both degrade gracefully.
if(SICNU_HAS_OPENCV)
    target_compile_definitions(qgis_app_georef PUBLIC SICNU_HAS_OPENCV=1)
    target_link_libraries(qgis_app_georef PUBLIC ${OpenCV_LIBS})
    target_include_directories(qgis_app_georef PUBLIC ${OpenCV_INCLUDE_DIRS})
endif()
