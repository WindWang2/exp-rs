###############################################################################
# src/grader/CMakeLists.txt — Process-aware Experiment Grader (RS14-05)
#
# sicnu_grader: STATIC leaf library. Pure C++20 + jsoncpp — zero Qt, zero
# GDAL, zero sicnu dependencies (same leaf discipline as the sibling light
# lanes). Everything the grader needs arrives as versioned JSON documents;
# it performs no I/O and owns no stores (ADR 0174).
###############################################################################

set( SICNU_GRADER_SOURCES
  grader_error.cpp
  grader_sha256.cpp
  grader_json.cpp
  grader_types.cpp
  grader_engine.cpp
)

add_library( sicnu_grader STATIC ${SICNU_GRADER_SOURCES} )
target_link_libraries( sicnu_grader PUBLIC jsoncpp )
target_include_directories( sicnu_grader PUBLIC ${CMAKE_SOURCE_DIR}/src )
target_compile_features( sicnu_grader PUBLIC cxx_std_20 )
