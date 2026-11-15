cmake_minimum_required(VERSION 3.21)
project(SicnuPiRuntimePackageProbe NONE)

# Exercise the production install rules without configuring Qt/QGIS.
find_package(Python REQUIRED COMPONENTS Interpreter)
set(SICNU_BUNDLE_PI ON CACHE BOOL "Install the pinned Pi SDK and product wrapper")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PiRuntime.cmake")

set(CPACK_PACKAGE_NAME "sicnu-pi-runtime")
set(CPACK_PACKAGE_VERSION "1.5.2")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR ZIP)
include(CPack)
