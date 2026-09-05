# QGIS_OUTPUT_DIRECTORY is now defined centrally in the root CMakeLists.txt.
# Keep a local guard for standalone/traversal builds.
if(NOT DEFINED QGIS_OUTPUT_DIRECTORY OR QGIS_OUTPUT_DIRECTORY STREQUAL "")
  set(QGIS_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
if(QGIS_OUTPUT_DIRECTORY STREQUAL "")
  message(FATAL_ERROR "QGIS_OUTPUT_DIRECTORY must be non-empty")
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QGIS_OUTPUT_DIRECTORY}/${QGIS_PLUGIN_SUBDIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QGIS_OUTPUT_DIRECTORY}/${QGIS_PLUGIN_SUBDIR}")

add_subdirectory(framework)
add_subdirectory(layer_tree)
add_subdirectory(processing)
