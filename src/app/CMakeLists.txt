add_executable(sicnu_geo_rs
    main.cpp
    main_window.cpp
    layer_tree_menu.cpp
    crs_presets.cpp
    dialogs/band_math_dialog.cpp
    dialogs/spectral_index_dialog.cpp
    dialogs/atmospheric_dialog.cpp
    dialogs/crs_preset_dialog.cpp
    dialogs/preferences_dialog.cpp
    dialogs/contrast_stretch_dialog.cpp
    dialogs/spatial_filter_dialog.cpp
    dialogs/pca_dialog.cpp
    dialogs/band_ratio_dialog.cpp
    map_tools/measure_tool.cpp
    widgets/histogram_widget.cpp
    widgets/spectral_profile_widget.cpp
    widgets/progress_dialog.cpp
    log_panel.cpp
    ${CMAKE_SOURCE_DIR}/resources/icons.qrc
)

target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/map_tools
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${GDAL_INCLUDE_DIR}
)

target_link_libraries(sicnu_geo_rs PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    Qt6::Sql
    qgis_core
    qgis_gui
    qgis_native
    qgis_app_georef
    sicnu_core
    sicnu_processing
    CURL::libcurl
    ${PCRE2_16}
    ${PCRE2_8}
)

set_target_properties(sicnu_geo_rs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_geo_rs"
)

install(TARGETS sicnu_geo_rs
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# Install resources
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/fonts
        DESTINATION share/sicnu_geo_rs/resources)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/icons
        DESTINATION share/sicnu_geo_rs/resources)
install(FILES ${CMAKE_SOURCE_DIR}/resources/styles.qss
        DESTINATION share/sicnu_geo_rs/resources)
install(FILES ${CMAKE_SOURCE_DIR}/resources/icons.qrc
        DESTINATION share/sicnu_geo_rs/resources)

# Install QGIS reference data (symbology, projections)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/qgis_ref/resources
        DESTINATION share/sicnu_geo_rs/qgis_ref
        PATTERN "*.db" EXCLUDE)

# Install desktop file (only if packaging files exist)
if(EXISTS ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.desktop)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.desktop
            DESTINATION share/applications)
endif()

# Install app icon
if(EXISTS ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.svg)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
