# SICNU GEO RS — LabSpec v3 lab runtime (Qt-free; jsoncpp + std only).
# Owns the runtime-capable LabSpec generation (spec_version 3 `runtime` block),
# the student lab-session state machine and its durable store. Metadata layer:
# no raster IO, no operator execution, no network. ADR 0174.
add_library(sicnu_lab_runtime STATIC
  sha256.cpp
  session_state.cpp
  session_store.cpp
  spec_runtime.cpp
)
target_include_directories(sicnu_lab_runtime PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# Link jsoncpp the same way sicnu_jobs / sicnu_workflow do in this repo
if(TARGET jsoncpp)
  target_link_libraries(sicnu_lab_runtime PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_lab_runtime PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_lab_runtime PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_lab_runtime PUBLIC jsoncpp)
endif()
set_target_properties(sicnu_lab_runtime PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
