# GUI-free Python plugin hosting core (ADR 0023): out-of-process worker pool,
# IPC server, app-interface proxy/bridge, and the plugin adapter/host.
qt_add_library(sicnu_python_isolated STATIC
    app_interface_bridge.cpp
    python_app_interface_proxy.cpp
    python_ipc_server.cpp
    python_plugin_adapter.cpp
    python_worker_process.cpp
    python_worker_process_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)

target_include_directories(sicnu_python_isolated PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(sicnu_python_isolated PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    sicnu_processing
    Sicnu::data
    qgis_core
    qgis_gui
)
