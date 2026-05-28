cmake_minimum_required(VERSION 3.20)
project(sicnu_geo VERSION 1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CheckFunctionExists)
include(CreateQgsVersion)
include(GenerateExportHeader)

set(COMPLETE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(ENABLE_LOCAL_BUILD_SHORTCUTS ON)
CREATE_QGSVERSION()

set(QT_VERSION_BASE "Qt6")
set(QT_VERSION_BASE_LOWER "qt6")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg Xml Sql Multimedia Core5Compat)
find_package(GDAL REQUIRED)
find_package(PROJ REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(GEOS REQUIRED)
find_package(EXPAT REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibZip REQUIRED)
find_package(ZSTD REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Qt6Keychain QUIET)
# Python (build-time only — code generation scripts, not runtime)
find_package(Python COMPONENTS Interpreter REQUIRED)
# QCA — include directly from known install path
include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindQCA.cmake OPTIONAL)
include_directories(SYSTEM /usr/include/qt6/Qca-qt6/QtCrypto)

# Internal nlohmann_json: create interface target for vendored header
if(WITH_INTERNAL_NLOHMANN_JSON)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/external/nlohmann)
  add_library(nlohmann_json INTERFACE)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
else()
  find_package(nlohmann_json REQUIRED)
endif()

# Internal spatialindex
option(WITH_INTERNAL_SPATIALINDEX "" ON)
if(WITH_INTERNAL_SPATIALINDEX)
  set(SPATIALINDEX_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/spatialindex/include)
  include_directories(SYSTEM ${SPATIALINDEX_INCLUDE_DIR})
endif()

# --- qgsconfig.h: all optional features OFF for P0 ---
# SICNU_GEO_RS: P3 — enable HAVE_GUI so core exposes authMethodEditWidget etc.
set(HAVE_GUI TRUE)
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(RELEASE_NAME "SICNU GEO RS")
set(QGIS_VERSION_INT 10000)
set(QGIS_PLUGIN_SUBDIR "plugins")
set(QGIS_DATA_SUBDIR "share")
set(QGIS_LIBEXEC_SUBDIR "libexec")
set(QGIS_LIB_SUBDIR "lib")
set(QGIS_QML_SUBDIR "qml")
set(QGIS_SERVER_MODULE_SUBDIR "server")
set(QGIS_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include/qgis")
set(QGIS_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(QGIS_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib")
set(QGISPOSTFIX "")
set(LIBRARY_TYPE SHARED)

# --- P0 options: disable optional features, use vendored deps ---
option(ENABLE_UNITY_BUILDS "" OFF)
option(WITH_INTERNAL_MESHOPTIMIZER "" ON)
option(WITH_VECTORTILE "" ON)
option(WITH_SFCGAL "" OFF)
option(USE_PRECOMPILED_HEADERS "" OFF)
option(WITH_PDAL "" OFF)
option(WITH_COPC "" ON)
option(WITH_EPT "" ON)
option(WITH_POSTGRESQL "" OFF)
option(WITH_SPATIALITE "" OFF)
option(WITH_ORACLE "" OFF)
option(WITH_HANA "" OFF)
option(WITH_MESH_EXTRAS "" OFF)
option(WITH_OPENCL "" OFF)
option(WITH_3D "" OFF)
option(WITH_QTWEBENGINE "" OFF)
option(WITH_GRASS "" OFF)
option(WITH_SERVER "" OFF)
option(WITH_GUI "" ON)
option(WITH_ANALYSIS "" OFF)
option(WITH_BINDINGS "" OFF)
option(WITH_AUTH "" ON)
if(WITH_AUTH)
  set(HAVE_AUTH TRUE)
endif()
if(WITH_EPT)
  set(HAVE_EPT TRUE)
endif()
if(WITH_COPC)
  set(HAVE_COPC TRUE)
endif()
option(ENABLE_TESTS "" OFF)
option(WITH_DRACO "" OFF)
option(WITH_INTERNAL_POLY2TRI "" ON)
option(WITH_INTERNAL_NLOHMANN_JSON "" ON)
option(WITH_INTERNAL_LAZPERF "" ON)
if(WITH_INTERNAL_LAZPERF)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/external/lazperf)
endif()

# configure_file AFTER all HAVE_* variables are resolved
configure_file(cmake_templates/qgsconfig.h.in ${CMAKE_BINARY_DIR}/qgsconfig.h)
include_directories(${CMAKE_BINARY_DIR})

add_subdirectory(src/core)
# SICNU_GEO_RS: P3 GUI options — disable optional deps not installed on this system
option(WITH_QWTPOLAR      "" OFF)
option(WITH_QSCINTILLA    "" OFF)
option(WITH_QTWEBENGINE   "" OFF)
option(WITH_QTGAMEPAD     "" OFF)
option(WITH_QUICK         "" OFF)
add_subdirectory(src/ui)
add_subdirectory(src/native)
add_subdirectory(src/gui)
# SICNU_GEO_RS: extra include paths needed by unconditional core sources
target_include_directories(qgis_core SYSTEM PRIVATE /usr/include/qt6/Qca-qt6/QtCrypto)
target_include_directories(qgis_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core/providers/vpc
  ${CMAKE_SOURCE_DIR}/src/core/providers/copc
  ${CMAKE_SOURCE_DIR}/src/core/providers/ept
)

# SICNU_GEO_RS: Pure C++ QGIS Application
if(WITH_GUI)
  set(SICNU_GEO_RS_CPP_SRCS
    main.cpp
    src/processing/sicnunativealgorithms.cpp
  )

  add_executable(sicnu_geo_rs ${SICNU_GEO_RS_CPP_SRCS})

  target_link_libraries(sicnu_geo_rs
    qgis_core
    qgis_gui
    qgis_native
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    Qt6::Sql
  )

  target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
  )

  # Set output directory
  set_target_properties(sicnu_geo_rs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_geo_rs"
  )

  # Install target
  install(TARGETS sicnu_geo_rs
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
  )
endif()
