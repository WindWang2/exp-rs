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
    qgsgeoreferencermainwindow.cpp
    rs_georef_mode_toggle.cpp
    rs_twincanvas_sync_controller.cpp
    qgsgeorefdatapoint.cpp
    qgsgeorefdelegates.cpp
    qgsgeoreftooladdpoint.cpp
    qgsgeoreftooldeletepoint.cpp
    qgsgeoreftoolmovepoint.cpp
    qgsmapcoordsdialog.cpp
    qgsgeorefvalidators.cpp
    rs_rms_scatter_widget.cpp
    rs_georef_params_panel.cpp
    rs_warp_task.cpp
)

target_include_directories(qgis_app_georef PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
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
