add_library(processing_plugin SHARED
    processing_plugin.cpp
)

target_include_directories(processing_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(processing_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
)

install(TARGETS processing_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
