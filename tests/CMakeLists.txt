# SICNU GEO RS — Unit Tests (Catch2)
include(Catch)

# Wrapper around catch_discover_tests that injects LSAN_OPTIONS when sanitizers
# are enabled, suppressing known singleton leaks (QgsExpression functions,
# QgsNumericFormatRegistry, Nvidia EGL driver).
function(sicnu_discover_tests NAME)
  if(ENABLE_SANITIZERS)
    # PRE_TEST mode runs test discovery at ctest time (not build time), so
    # ENVIRONMENT_MODIFICATION is applied to the discovery process.
    # This prevents ASAN leak reports from failing test discovery.
    catch_discover_tests(${NAME}
      DISCOVERY_MODE PRE_TEST
      PROPERTIES ENVIRONMENT_MODIFICATION "LSAN_OPTIONS=set:detect_leaks=0")
  else()
    # PRE_TEST avoids race conditions in parallel builds where discovery
    # tries to run the test executable before it is linked.
    catch_discover_tests(${NAME}
      DISCOVERY_MODE PRE_TEST)
  endif()
endfunction()

# Helper function to add a test executable
function(sicnu_add_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(${NAME})
endfunction()

# Smoke tests
sicnu_add_test(test_smoke)
sicnu_add_test(test_crs)
sicnu_add_test(test_geometry)
sicnu_add_test(test_layers)

# test_paths has custom main (needs QCoreApplication for AppPaths)
add_executable(test_paths test_paths.cpp)
target_link_libraries(test_paths PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_paths PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_paths)

# Refactor tests — verify main.cpp split structure
sicnu_add_test(test_refactor)

# Consolidation tests — verify provider merge
add_executable(test_consolidate test_consolidate.cpp)
target_link_libraries(test_consolidate PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_consolidate PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_consolidate)

# Atmospheric correction tests
add_executable(test_atmospheric test_atmospheric.cpp)
target_link_libraries(test_atmospheric PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_atmospheric PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_atmospheric)

# Band math tests
add_executable(test_band_math test_band_math.cpp)
target_link_libraries(test_band_math PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_band_math PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_band_math)

# Spectral index tests
add_executable(test_spectral_indices test_spectral_indices.cpp)
target_link_libraries(test_spectral_indices PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_spectral_indices PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_indices)

# Raster NDVI algorithm tests
add_executable(test_raster_ndvi test_raster_ndvi.cpp)
target_link_libraries(test_raster_ndvi PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_raster_ndvi PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_ndvi)

# GDAL error handler tests
add_executable(test_gdal_errors test_gdal_errors.cpp)
target_link_libraries(test_gdal_errors PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_errors PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_errors)

# GDAL tool wrapper tests — verify error message capture
add_executable(test_gdal_tool_wrapper test_gdal_tool_wrapper.cpp)
target_link_libraries(test_gdal_tool_wrapper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_tool_wrapper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_tool_wrapper)

# GDAL null band handle tests
add_executable(test_gdal_null_band test_gdal_null_band.cpp)
target_link_libraries(test_gdal_null_band PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_gdal_null_band PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_null_band)

# GDAL thread safety tests
add_executable(test_gdal_thread_safety test_gdal_thread_safety.cpp)
target_link_libraries(test_gdal_thread_safety PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_thread_safety PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_thread_safety)

# GDAL wrapper tests — verify GDAL C API wrapper
add_executable(test_gdal_wrapper test_gdal_wrapper.cpp)
target_link_libraries(test_gdal_wrapper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_wrapper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_wrapper)

# Colormap tests — verify color ramps loaded from style database
# Custom main: needs QgsApplication with correct prefix path
add_executable(test_colormap test_colormap.cpp)
target_link_libraries(test_colormap PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_colormap PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_colormap)

# Processing tests — link sicnu_processing for provider/algorithm tests
add_executable(test_processing test_processing.cpp)
target_link_libraries(test_processing PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing)

# Algorithm organization tests — verify metadata quality
add_executable(test_algorithm_organization test_algorithm_organization.cpp)
target_link_libraries(test_algorithm_organization PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_algorithm_organization PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_algorithm_organization)

# Progress dialog tests
add_executable(test_progress_dialog test_progress_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/progress_dialog.cpp
)
target_link_libraries(test_progress_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_progress_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_progress_dialog)

# Spectral profile widget tests
add_executable(test_spectral_profile_widget test_spectral_profile_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/spectral_profile_widget.cpp
)
target_link_libraries(test_spectral_profile_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_spectral_profile_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_profile_widget)

# Panel persistence tests
add_executable(test_panel_persistence test_panel_persistence.cpp)
target_link_libraries(test_panel_persistence PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_panel_persistence PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_panel_persistence)

# Preferences dialog tests
add_executable(test_preferences_dialog test_preferences_dialog.cpp)
target_link_libraries(test_preferences_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_preferences_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_preferences_dialog)

# Processing framework tests (cache, progress, errors)
add_executable(test_processing_framework test_processing_framework.cpp)
target_link_libraries(test_processing_framework PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing_framework PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_framework)

# Processing dialog tests
add_executable(test_processing_dialog test_processing_dialog.cpp)
target_link_libraries(test_processing_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_dialog)

# Advanced algorithms tests
add_executable(test_advanced_algorithms test_advanced_algorithms.cpp)
target_link_libraries(test_advanced_algorithms PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_advanced_algorithms PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_advanced_algorithms)

# Data format tests (GDAL drivers)
add_executable(test_data_formats test_data_formats.cpp)
target_link_libraries(test_data_formats PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_data_formats PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_data_formats)

# Vector layer properties tests
sicnu_add_test(test_vector_properties)

# Layer tree bridge tests
sicnu_add_test(test_layer_tree_bridge)

# Change detection tests
add_executable(test_change_detection test_change_detection.cpp)
target_link_libraries(test_change_detection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_change_detection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_change_detection)

# Mosaic tests
add_executable(test_mosaic test_mosaic.cpp)
target_link_libraries(test_mosaic PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_mosaic PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mosaic)

# Processing integration tests
add_executable(test_processing_integration test_processing_integration.cpp)
target_link_libraries(test_processing_integration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing_integration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_integration)

# Log panel tests
add_executable(test_log_panel test_log_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/log_panel.cpp
)
target_link_libraries(test_log_panel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_log_panel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_log_panel)

# Image enhancement tests
add_executable(test_image_enhancement test_image_enhancement.cpp)
target_link_libraries(test_image_enhancement PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_image_enhancement PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_enhancement)

# PCA tests
add_executable(test_pca test_pca.cpp)
target_link_libraries(test_pca PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_pca PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_pca)

# Spatial filter tests
add_executable(test_spatial_filter test_spatial_filter.cpp)
target_link_libraries(test_spatial_filter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_spatial_filter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spatial_filter)

# Georeferencer least-squares tests
add_executable(test_least_squares test_least_squares.cpp)
target_link_libraries(test_least_squares PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
target_include_directories(test_least_squares PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_least_squares)

# Georeferencer GCP transformer tests
add_executable(test_gcp_transformer test_gcp_transformer.cpp)
target_link_libraries(test_gcp_transformer PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
target_include_directories(test_gcp_transformer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_transformer)

# Georeferencer image warper tests (Task 11.4.2)
add_executable(test_image_warper test_image_warper.cpp)
target_link_libraries(test_image_warper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper)

add_executable(test_image_warper_cancel test_image_warper_cancel.cpp)
target_link_libraries(test_image_warper_cancel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Concurrent
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper_cancel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper_cancel)

add_executable(test_image_warper_errors test_image_warper_errors.cpp)
target_link_libraries(test_image_warper_errors PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper_errors PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper_errors)

add_executable(test_warp_crs_passthrough test_warp_crs_passthrough.cpp)
target_link_libraries(test_warp_crs_passthrough PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_warp_crs_passthrough PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_warp_crs_passthrough)

# Georeferencer GCP list tests (Task 11.4.3) — signal emission, residual skip-disabled
add_executable(test_gcp_list test_gcp_list.cpp)
target_link_libraries(test_gcp_list PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_list PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_list)

# Georeferencer .points v2 file tests (Task 11.4.3)
add_executable(test_gcp_points_file test_gcp_points_file.cpp)
target_link_libraries(test_gcp_points_file PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_points_file PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_points_file)

# Georeferencer main window shell tests (Task 11.4.4)
add_executable(test_georef_window test_georef_window.cpp)
target_link_libraries(test_georef_window PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_window PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window)

# GCP table widget tests (Task 11.4.6)
add_executable(test_gcp_list_widget test_gcp_list_widget.cpp)
target_link_libraries(test_gcp_list_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_list_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_list_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_list_widget)

# Twin canvas sync controller tests (Task 11.4.5)
add_executable(test_twincanvas_sync test_twincanvas_sync.cpp)
target_link_libraries(test_twincanvas_sync PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_twincanvas_sync PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_twincanvas_sync)

# RMS scatter widget tests (Task 11.4.7)
add_executable(test_rms_scatter test_rms_scatter.cpp)
target_link_libraries(test_rms_scatter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_rms_scatter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rms_scatter)

# Warp-in-progress UI lock tests (Task 11.4.7)
add_executable(test_georef_window_warp_lock test_georef_window_warp_lock.cpp)
target_link_libraries(test_georef_window_warp_lock PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_window_warp_lock PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window_warp_lock PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window_warp_lock)

# Task 11.4.8 — RPC physical model transformer tests
add_executable(test_rpc_transformer test_rpc_transformer.cpp)
target_link_libraries(test_rpc_transformer PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_rpc_transformer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_transformer)

# Task 11.4.8 — RPC mode UI test (DEM section visibility on toggle)
add_executable(test_georef_window_rpc_mode test_georef_window_rpc_mode.cpp)
target_link_libraries(test_georef_window_rpc_mode PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_window_rpc_mode PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window_rpc_mode PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window_rpc_mode)

# Task 11.5.4 — DEM Z-offset wired into RPC pipeline
add_executable(test_dem_z_offset test_dem_z_offset.cpp)
target_link_libraries(test_dem_z_offset PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_dem_z_offset PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_dem_z_offset)

# Task 11.5.5 — RPC GCP linear-bias refinement
add_executable(test_rpc_gcp_refine test_rpc_gcp_refine.cpp)
target_link_libraries(test_rpc_gcp_refine PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_rpc_gcp_refine PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_gcp_refine)

# Task 11.5.1 — CRS picker persistence + signal tests
add_executable(test_crs_picker_persists test_crs_picker_persists.cpp)
target_link_libraries(test_crs_picker_persists PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_crs_picker_persists PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_crs_picker_persists PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_crs_picker_persists)

# Task 11.5.3 — Image-to-Image mode load test
add_executable(test_image_to_image_load test_image_to_image_load.cpp)
target_link_libraries(test_image_to_image_load PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_to_image_load PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_image_to_image_load PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_image_to_image_load)

# Task 11.5.2 — GCP canvas item paint smoke test
add_executable(test_gcp_canvas_item test_gcp_canvas_item.cpp)
target_link_libraries(test_gcp_canvas_item PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_canvas_item PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_canvas_item PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_canvas_item)

# Task 11.5.6 — RPC golden warp regression (synthetic non-trivial RPC + DEM)
add_executable(test_rpc_golden test_rpc_golden.cpp)
target_compile_definitions(test_rpc_golden PRIVATE
  GEOREF_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data/georef")
target_link_libraries(test_rpc_golden PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_rpc_golden PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_golden)

# Task 11.5.7 — SIFT auto-match matcher tests
add_executable(test_sift_matcher test_sift_matcher.cpp)
target_link_libraries(test_sift_matcher PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_sift_matcher PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
if(SICNU_HAS_OPENCV)
  target_compile_definitions(test_sift_matcher PRIVATE SICNU_HAS_OPENCV=1)
  target_link_libraries(test_sift_matcher PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_sift_matcher PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
sicnu_discover_tests(test_sift_matcher)

# Phase 10A Task 10.1 — ROI collection unit tests
add_executable(test_roi_collection test_roi_collection.cpp)
target_link_libraries(test_roi_collection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
)
target_include_directories(test_roi_collection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_roi_collection PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_collection)

# Phase 10A Task 10.1 — ROI shapefile + sidecar JSON I/O tests
add_executable(test_roi_io test_roi_io.cpp)
target_link_libraries(test_roi_io PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_roi_io PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_roi_io PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_io)

# Band ratio and IHS transform tests
add_executable(test_band_ratio test_band_ratio.cpp)
target_link_libraries(test_band_ratio PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_band_ratio PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_band_ratio)

# Phase 10A Task 10.2 — Classification main window shell tests.
# Only built when OpenCV ml component is available (qgis_app_classify exists).
if(TARGET qgis_app_classify)
  add_executable(test_classification_window test_classification_window.cpp)
  target_link_libraries(test_classification_window PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_classification_window PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classification_window PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classification_window)

  # Phase 10A Task 10.3 — Class table widget tests.
  add_executable(test_class_table_widget test_class_table_widget.cpp)
  target_link_libraries(test_class_table_widget PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_class_table_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_class_table_widget PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_class_table_widget)

  # Phase 10A Task 10.4 — Pixel rasterizer (geometry -> pixel index set)
  add_executable(test_pixel_rasterizer test_pixel_rasterizer.cpp)
  target_link_libraries(test_pixel_rasterizer PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    qgis_analysis
    GDAL::GDAL
  )
  target_include_directories(test_pixel_rasterizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_pixel_rasterizer)

  # Phase 10A Task 10.4 — ROI polygon map tool (3 clicks + dblclk -> emit)
  add_executable(test_roi_tool_polygon test_roi_tool_polygon.cpp)
  target_link_libraries(test_roi_tool_polygon PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_roi_tool_polygon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_roi_tool_polygon PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_roi_tool_polygon)

  # Phase 10A Task 10.5 — Spectral curve widget tests.
  add_executable(test_spectral_curve test_spectral_curve.cpp)
  target_link_libraries(test_spectral_curve PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_spectral_curve PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_spectral_curve PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_spectral_curve)

  # Phase 10A Task 10.6 — Jeffries-Matusita separability tests.
  add_executable(test_jm_separability test_jm_separability.cpp)
  target_link_libraries(test_jm_separability PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_jm_separability PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_jm_separability)

  # Phase 10A Task 10.7 — Magic-wand flood-fill algorithm tests.
  add_executable(test_flood_fill test_flood_fill.cpp)
  target_link_libraries(test_flood_fill PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_flood_fill PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_flood_fill)

  # Phase 10A Task 10.8 — NormalBayes backend accuracy test.
  add_executable(test_classifier_normalbayes test_classifier_normalbayes.cpp)
  target_link_libraries(test_classifier_normalbayes PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_normalbayes PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_normalbayes)

  # Phase 10A Task 10.8 — SVM (RBF) backend accuracy test.
  add_executable(test_classifier_svm test_classifier_svm.cpp)
  target_link_libraries(test_classifier_svm PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_svm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_svm)

  # Phase 10A Task 10.8 — KMeans backend cluster-centre test.
  add_executable(test_classifier_kmeans test_classifier_kmeans.cpp)
  target_link_libraries(test_classifier_kmeans PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_kmeans PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_kmeans)

  # Phase 10A review patch — stratified train/test split helper test.
  add_executable(test_stratified_split test_stratified_split.cpp)
  target_link_libraries(test_stratified_split PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_stratified_split PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_stratified_split)

  # Phase 10A Task 10.9 — confusion matrix + Kappa accuracy assessment.
  add_executable(test_accuracy_assessment test_accuracy_assessment.cpp)
  target_link_libraries(test_accuracy_assessment PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_accuracy_assessment PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_accuracy_assessment)

  # Phase 10A.1.1 — Hungarian min-cost assignment (Munkres O(n^3)).
  add_executable(test_hungarian_assignment test_hungarian_assignment.cpp)
  target_link_libraries(test_hungarian_assignment PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_hungarian_assignment PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_hungarian_assignment)

  # Phase 10A Task 10.8 — End-to-end classification task test.
  add_executable(test_classification_e2e test_classification_e2e.cpp)
  target_link_libraries(test_classification_e2e PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    qgis_app_classify
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classification_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classification_e2e PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classification_e2e)
endif()
