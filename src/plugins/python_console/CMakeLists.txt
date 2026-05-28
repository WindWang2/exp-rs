add_library(python_console_plugin SHARED
    python_console_plugin.cpp
)

target_include_directories(python_console_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(python_console_plugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    pybind11::embed
)

# Python files need QT_NO_KEYWORDS
set_source_files_properties(
    python_console_plugin.cpp
    PROPERTIES COMPILE_DEFINITIONS "QT_NO_KEYWORDS"
)

install(TARGETS python_console_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
