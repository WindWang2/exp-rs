cmake_minimum_required(VERSION 3.21)
project(SicnuPythonRuntimePackageProbe NONE)

# Exercise the production install rules without configuring Qt/QGIS.
find_package(Python REQUIRED COMPONENTS Interpreter)
set(SICNU_BUNDLE_PYTHON ON CACHE BOOL "Install the pinned Python runtime")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PythonRuntime.cmake")

# Archive the same install rules, including their install-time validation.
set(CPACK_PACKAGE_NAME "sicnu-python-runtime")
set(CPACK_PACKAGE_VERSION "3.12.16")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
