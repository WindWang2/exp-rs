# sicnu_runtime — Qt-free runtime services of the Execution & Data Plane 3.0:
# chunk/tile execution graph, later the GPU plane / worker host / telemetry.
# Kept leaf-level (no Qt, no GDAL) so execution-framework consumers and tests
# can link it without dragging GUI or provider dependencies.
add_library(sicnu_runtime STATIC
    chunk/chunk_pipeline.cpp
    gpu/gpu_plane.cpp
    observability/execution_telemetry.cpp
)
target_include_directories(sicnu_runtime PUBLIC ${CMAKE_SOURCE_DIR}/src)
# Linked into SHARED consumers; force PIC like the other static leaves.
set_target_properties(sicnu_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(sicnu_runtime PUBLIC Threads::Threads)
