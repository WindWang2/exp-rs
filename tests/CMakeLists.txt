# SICNU GEO RS — Unit Tests (Catch2)
include(Catch)

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
  catch_discover_tests(${NAME})
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
catch_discover_tests(test_paths)

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
catch_discover_tests(test_consolidate)

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
catch_discover_tests(test_atmospheric)

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
catch_discover_tests(test_band_math)

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
catch_discover_tests(test_spectral_indices)

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
catch_discover_tests(test_raster_ndvi)

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
catch_discover_tests(test_gdal_errors)

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
catch_discover_tests(test_gdal_tool_wrapper)

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
catch_discover_tests(test_gdal_null_band)

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
catch_discover_tests(test_gdal_thread_safety)

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
catch_discover_tests(test_gdal_wrapper)

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
catch_discover_tests(test_colormap)

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
catch_discover_tests(test_processing)

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
catch_discover_tests(test_algorithm_organization)

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
catch_discover_tests(test_progress_dialog)

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
catch_discover_tests(test_spectral_profile_widget)

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
catch_discover_tests(test_panel_persistence)

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
catch_discover_tests(test_preferences_dialog)

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
catch_discover_tests(test_processing_framework)

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
catch_discover_tests(test_processing_dialog)

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
catch_discover_tests(test_advanced_algorithms)

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
catch_discover_tests(test_data_formats)

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
catch_discover_tests(test_change_detection)

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
catch_discover_tests(test_mosaic)

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
catch_discover_tests(test_processing_integration)

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
catch_discover_tests(test_log_panel)
