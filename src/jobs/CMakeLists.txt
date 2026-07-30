# SICNU GEO RS — Unified JobEngine (Qt-free; callbacks + mutex)
add_library(sicnu_jobs STATIC job_engine.cpp)
target_include_directories(sicnu_jobs PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# Link jsoncpp the same way sicnu_workflow / sicnu_operators do in this repo
if(TARGET jsoncpp)
  target_link_libraries(sicnu_jobs PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_jobs PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_jobs PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_jobs PUBLIC jsoncpp)
endif()
if(TARGET sicnu_operators)
  target_link_libraries(sicnu_jobs PUBLIC sicnu_operators)
endif()
set_target_properties(sicnu_jobs PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
