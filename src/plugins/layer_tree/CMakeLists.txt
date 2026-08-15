add_library(layer_tree_plugin SHARED
    layer_tree_plugin.cpp
)

target_include_directories(layer_tree_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer_tree_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
)

install(TARGETS layer_tree_plugin
    RUNTIME DESTINATION ${QGIS_BIN_DIR}
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
    ARCHIVE DESTINATION ${QGIS_LIB_DIR}
)
