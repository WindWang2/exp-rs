qt_add_library(sicnu_qgis_display STATIC
    data_project_serializer.cpp
    data_project_serializer.h
    display/qgis_display_manager.cpp
    display/auth_resolver.cpp
    display/auth_resolver.h
    display/remote_map_uri.h
    display/qgis_display_manager.h
    project_context.cpp
    project_context.h
)
add_library(Sicnu::qgis_display ALIAS sicnu_qgis_display)
target_include_directories(sicnu_qgis_display
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(sicnu_qgis_display
    PUBLIC
        Qt6::Core
        Sicnu::data
    PRIVATE
        Qt6::Gui
        Qt6::Widgets
        Qt6::Xml
        qgis_core
        qgis_gui
)
set_target_properties(sicnu_qgis_display PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(sicnu_geo_rs
    main.cpp
    main_window.cpp
    main_window_menus.cpp
    main_window_docks.cpp
    main_window_connections.cpp
    main_window_vector.cpp
    main_window_project.cpp
    main_window_layers.cpp
    main_window_view.cpp
    main_window_misc.cpp
    main_window_processing.cpp
    shell/schema_form_builder.cpp
    shell/task_panel_host.cpp
    shell/workflow_session_controller.cpp
    shell/ribbon_controller.cpp
    shell/job_engine_qt_bridge.cpp
    shell/rs_job_runner.cpp
    shell/rs_session_map_workspace.cpp
    shell/processing_job_adapter.cpp
    shell/rs_job_panel.cpp
    layer_tree_menu.cpp
    crs_presets.cpp
    log_panel.cpp
    dialogs/band_math_dialog.cpp
    dialogs/spectral_index_dialog.cpp
    dialogs/atmospheric_dialog.cpp
    dialogs/crs_preset_dialog.cpp
    dialogs/preferences_dialog.cpp
    dialogs/contrast_stretch_dialog.cpp
    dialogs/spatial_filter_dialog.cpp
    dialogs/speckle_filter_dialog.cpp
    dialogs/pca_dialog.cpp
    dialogs/band_ratio_dialog.cpp
    dialogs/terrain_dialog.cpp
    dialogs/fusion_dialog.cpp
    dialogs/sicnu_algorithm_dialog.cpp
    dialogs/mosaic_dialog.cpp
    dialogs/change_detection_dialog.cpp
    dialogs/dialog_utils.cpp
    dialogs/dialog_help_catalog.cpp
    dialogs/comparison_dialog.cpp
    dialogs/batch_processing_dialog.cpp
    dialogs/stac_browser_dialog.cpp
    dialogs/raster_processing_dialog_base.cpp
    dialogs/extract_band_dialog.cpp
    dialogs/landsat_import_dialog.cpp
    layout/qgslayoutdesignerdialog.cpp
    map_tools/measure_tool.cpp
    widgets/histogram_widget.cpp
    widgets/histogram_stretch_widget.cpp
    widgets/spectral_profile_widget.cpp
    widgets/progress_dialog.cpp
    widgets/comparison_widget.cpp
    widgets/guided_workflow_widget.cpp
    panels/data_manager_panel.cpp

    # QgisApp facade
    qgis_app_facade.cpp

    # Layer and map tool managers
    layer_manager.cpp
    map_tools/map_tool_manager.cpp
    map_tools/swipe_map_tool.cpp

    # MCP server + STAC client
    ${CMAKE_SOURCE_DIR}/src/agent/mcp_server.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/stac_client.cpp

    # Image enhancement panel
    dialogs/image_enhancement_panel.cpp

    # Vector editing map tools
    qgsmaptooladdfeature.cpp
    qgsmaptooladdpart.cpp
    qgsmaptooladdring.cpp
    qgsmaptoolchamferfillet.cpp
    qgsmaptooldeletepart.cpp
    qgsmaptooldeletering.cpp
    qgsmaptoolfeatureaction.cpp
    qgsmaptoolfeaturearray.cpp
    qgsmaptoolfillring.cpp
    qgsmaptoolmovefeature.cpp
    qgsmaptooloffsetcurve.cpp
    qgsmaptoolreshape.cpp
    qgsmaptoolreverseline.cpp
    qgsmaptoolrotatefeature.cpp
    qgsmaptoolscalefeature.cpp
    qgsmaptoolsimplify.cpp
    qgsmaptoolsplitfeatures.cpp
    qgsmaptoolsplitparts.cpp
    qgsmaptooltrimextendfeature.cpp
    qgsdisplayangle.cpp
    qgspointmarkeritem.cpp
    qgspointrotationitem.cpp

    # Vector editing infrastructure
    qgsfeatureaction.cpp
    qgsguivectorlayertools.cpp
    qgsclipboard.cpp
    qgsundowidget.cpp
    qgsmergeattributesdialog.cpp
    qgsfixattributedialog.cpp
    qgsattributetabledialog.cpp
    qgssnappingwidget.cpp
    qgssnappinglayertreemodel.cpp
    qgsvariantdelegate.cpp
    qgsdelattrdialog.cpp
    qgsselectbyformdialog.cpp

    # Shape construction tools
    maptools/qgsavoidintersectionsoperation.cpp
    maptools/qgsmaptoolshapecircle2points.cpp
    maptools/qgsmaptoolshapecircle2tangentspoint.cpp
    maptools/qgsmaptoolshapecircle3points.cpp
    maptools/qgsmaptoolshapecircle3tangents.cpp
    maptools/qgsmaptoolshapecircleabstract.cpp
    maptools/qgsmaptoolshapecirclecenterpoint.cpp
    maptools/qgsmaptoolshapecircularstringabstract.cpp
    maptools/qgsmaptoolshapecircularstringradius.cpp
    # maptools/qgsmaptoolsdigitizingtechniquemanager.cpp  # needs deep QgisApp integration
    # maptools/qgsappmaptools.cpp  # needs deep QgisApp integration
    maptools/qgsmaptoolshapeellipseabstract.cpp
    maptools/qgsmaptoolshapeellipsecenter2points.cpp
    maptools/qgsmaptoolshapeellipsecenterpoint.cpp
    maptools/qgsmaptoolshapeellipseextent.cpp
    maptools/qgsmaptoolshapeellipsefoci.cpp
    maptools/qgsmaptoolshaperectangle3points.cpp
    maptools/qgsmaptoolshaperectangleabstract.cpp
    maptools/qgsmaptoolshaperectanglecenter.cpp
    maptools/qgsmaptoolshaperectangleextent.cpp
    maptools/qgsmaptoolshaperegularpolygon2points.cpp
    maptools/qgsmaptoolshaperegularpolygonabstract.cpp
    maptools/qgsmaptoolshaperegularpolygoncentercorner.cpp
    maptools/qgsmaptoolshaperegularpolygoncenterpoint.cpp

    # Vertex editing
    vertextool/qgslockedfeature.cpp
    vertextool/qgsvertexeditor.cpp
    vertextool/qgsvertextool.cpp

    # Selection tools
    selecttools/qgsmaptoolselect.cpp
    selecttools/qgsmaptoolselectionhandler.cpp
    selecttools/qgsmaptoolselectutils.cpp

    ${CMAKE_SOURCE_DIR}/resources/icons.qrc
)

target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/map_tools
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/maptools
    ${CMAKE_CURRENT_SOURCE_DIR}/vertextool
    ${CMAKE_CURRENT_SOURCE_DIR}/selecttools
    ${CMAKE_SOURCE_DIR}/src/agent
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
    Sicnu::qgis_display
    sicnu_core
    sicnu_processing
    sicnu_operators
    sicnu_workflow
    sicnu_jobs
    jsoncpp
    CURL::libcurl
    ${PCRE2_16}
    ${PCRE2_8}
)

if(SICNU_EMBED_PYTHON)
    target_sources(sicnu_geo_rs PRIVATE
        ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_console.cpp
        ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
        ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
    )
    target_include_directories(sicnu_geo_rs PRIVATE ${CMAKE_SOURCE_DIR}/src/python)
    target_link_libraries(sicnu_geo_rs PRIVATE Python::Python)
    target_compile_definitions(sicnu_geo_rs PRIVATE SICNU_EMBED_PYTHON=1)
endif()

# Phase 10A — Pixel-Based Classification. Link only when the static
# library was actually built (OpenCV ml component available).
if(TARGET qgis_app_classify)
    target_link_libraries(sicnu_geo_rs PRIVATE qgis_app_classify)
    target_compile_definitions(sicnu_geo_rs PRIVATE SICNU_HAS_CLASSIFY=1)
endif()

# Phase 10B — OBIA Classification. Link only when the static library
# was actually built (OpenCV ml component available).
if(TARGET qgis_app_obia)
    target_link_libraries(sicnu_geo_rs PRIVATE qgis_app_obia)
    target_compile_definitions(sicnu_geo_rs PRIVATE SICNU_HAS_OBIA=1)
endif()

set_target_properties(sicnu_geo_rs PROPERTIES
    AUTOUIC OFF
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
# Source lives under refs/qgis/; install path stays qgis_ref for runtime compatibility.
if(EXISTS ${CMAKE_SOURCE_DIR}/refs/qgis/resources)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/refs/qgis/resources
            DESTINATION share/sicnu_geo_rs/qgis_ref
            PATTERN "*.db" EXCLUDE)
elseif(EXISTS ${CMAKE_SOURCE_DIR}/qgis_ref/resources)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/qgis_ref/resources
            DESTINATION share/sicnu_geo_rs/qgis_ref
            PATTERN "*.db" EXCLUDE)
endif()

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
