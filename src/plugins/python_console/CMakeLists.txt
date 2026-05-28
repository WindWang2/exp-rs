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

# Python embedding conflicts with Qt keywords (signals/slots)
target_compile_definitions(python_console_plugin PRIVATE QT_NO_KEYWORDS)

install(TARGETS python_console_plugin
    LIBRARY DESTINATION ${QGIS_PLUGIN_SUBDIR}
)
