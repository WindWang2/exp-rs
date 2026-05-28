add_library(sicnu_gui SHARED
    main_window.cpp
)

target_include_directories(sicnu_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sicnu_gui PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_core
)
