# FakeDepConfigVersion.cmake — basic-package-version semantics for the fixture:
# compatible unless the request is strictly newer than 1.0.
set(PACKAGE_VERSION "1.0")
if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT TRUE)
endif()
if(NOT PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
