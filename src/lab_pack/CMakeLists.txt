###############################################################################
# src/lab_pack/CMakeLists.txt — sicnu.lab-pack/1 data pack contract leaf.
#
# sicnu_lab_pack: STATIC leaf library. Pure C++20 + jsoncpp — zero Qt, zero
# GDAL, zero other sicnu dependencies except the sibling grader leaf whose
# validated SHA-256 (FIPS 180-4, NIST vectors in test_grader_schema) it
# reuses, so the repo keeps exactly one Qt-free sha256 implementation instead
# of growing a second one. The ONE parser/validator for the pack contract:
# both sicnu::agent (Qt façade) and sicnu::teaching_admin delegate here.
###############################################################################

set( SICNU_LAB_PACK_SOURCES
  lab_pack.cpp
)

add_library( sicnu_lab_pack STATIC ${SICNU_LAB_PACK_SOURCES} )
target_link_libraries( sicnu_lab_pack PUBLIC jsoncpp sicnu_grader )
target_include_directories( sicnu_lab_pack PUBLIC ${CMAKE_SOURCE_DIR}/src )
target_compile_features( sicnu_lab_pack PUBLIC cxx_std_20 )
set_target_properties( sicnu_lab_pack PROPERTIES POSITION_INDEPENDENT_CODE ON )

# Layer guard — deliberately LAST in this file so it observes every link
# declared above (adding a link below this guard defeats it; keep new links in
# the add_library block). Qt/GDAL/app linkage would defeat the purpose of the
# Qt-free shared leaf.
get_target_property( SICNU_LAB_PACK_LINK_LIBRARIES sicnu_lab_pack LINK_LIBRARIES )
set( SICNU_LAB_PACK_ALLOWED_LINKS jsoncpp jsoncpp_lib sicnu_grader )
foreach( SICNU_LAB_PACK_LINK IN LISTS SICNU_LAB_PACK_LINK_LIBRARIES )
  if ( NOT SICNU_LAB_PACK_LINK IN_LIST SICNU_LAB_PACK_ALLOWED_LINKS )
    message( FATAL_ERROR
      "sicnu_lab_pack must link jsoncpp + sicnu_grader only — found '${SICNU_LAB_PACK_LINK}' (leaf discipline)" )
  endif()
endforeach()
