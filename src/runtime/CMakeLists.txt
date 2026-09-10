# sicnu_runtime — Qt-free runtime services of the Execution & Data Plane 3.0:
# chunk/tile execution graph, later the GPU plane / worker host / telemetry.
# Kept leaf-level (no Qt, no GDAL) so execution-framework consumers and tests
# can link it without dragging GUI or provider dependencies.
#
# SHARED (was STATIC): the fault-injection registry (Verification 7.0) must
# have exactly ONE process-wide instance — a static copy per consuming DLL
# would let tests arm one instance while production DLLs probe another. As a
# DLL, the registry state is shared across sicnu_processing / sicnu_data /
# sicnu_workflow / test executables. WINDOWS_EXPORT_ALL_SYMBOLS follows the
# sicnu_workflow precedent.
add_library(sicnu_runtime SHARED
    chunk/chunk_pipeline.cpp
    gpu/gpu_plane.cpp
    observability/execution_telemetry.cpp
    observability/fault_registry.cpp
    observability/trace.cpp
    observability/diagnostic_report.cpp
)
target_include_directories(sicnu_runtime PUBLIC ${CMAKE_SOURCE_DIR}/src)
set_target_properties(sicnu_runtime PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
find_package(Threads REQUIRED)
target_link_libraries(sicnu_runtime PUBLIC Threads::Threads)
# Mirrors the sicnu_workflow install contract so packaging keeps shipping the
# runtime next to the other shared libraries.
install(TARGETS sicnu_runtime
    RUNTIME DESTINATION ${QGIS_BIN_DIR}
    LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
    ARCHIVE DESTINATION ${QGIS_LIB_DIR}
)
