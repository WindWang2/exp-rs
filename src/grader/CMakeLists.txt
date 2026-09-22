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

# Layer guard: the grader leaf consumes recorded facts — including verifier
# verdicts — as versioned JSON documents (EvidenceKind::verifier_verdict); it
# never links the verifier to recompute it. Qt/GDAL/sicnu linkage would break
# the ADR 0174 leaf discipline.
get_target_property( SICNU_GRADER_LINK_LIBRARIES sicnu_grader LINK_LIBRARIES )
set( SICNU_GRADER_ALLOWED_LINKS jsoncpp jsoncpp_lib )
foreach( SICNU_GRADER_LINK IN LISTS SICNU_GRADER_LINK_LIBRARIES )
  if ( NOT SICNU_GRADER_LINK IN_LIST SICNU_GRADER_ALLOWED_LINKS )
    message( FATAL_ERROR
      "sicnu_grader must link jsoncpp only — found '${SICNU_GRADER_LINK}' (leaf discipline, ADR 0174)" )
  endif()
endforeach()
