# Archive the install tree of SICNU GEO RS. Runtime dependency deployment is
# handled by the platform packaging scripts; CPack owns this archive target.
include_guard(GLOBAL)

set(CPACK_PACKAGE_NAME "sicnu-geo-rs")
set(CPACK_PACKAGE_VENDOR "SICNU")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SICNU GEO RS remote sensing platform")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VERSION "${COMPLETE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SICNU GEO RS ${COMPLETE_VERSION}")
if(NOT CPACK_GENERATOR)
  if(WIN32)
    set(CPACK_GENERATOR "ZIP")
  else()
    set(CPACK_GENERATOR "TGZ")
  endif()
endif()

include(CPack)
