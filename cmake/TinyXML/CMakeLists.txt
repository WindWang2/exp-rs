# Minimal TinyXML build for OTB subproject integration
cmake_minimum_required(VERSION 3.13)
project(tinyxml_vendored LANGUAGES CXX)

# Ensure output goes to the correct directory (not /bin/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE PATH "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE PATH "" FORCE)

add_definitions(-DTIXML_USE_STL)
add_library(tinyxml_vendored STATIC
    src/tinyxml.cpp
    src/tinyxmlerror.cpp
    src/tinyxmlparser.cpp
    src/tinystr.cpp
)
target_include_directories(tinyxml_vendored PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# OTB modules use install(EXPORT ...) which requires all linked targets
# to be in an export set. Add tinyxml_vendored to a compatible export.
install(TARGETS tinyxml_vendored EXPORT TinyXMLTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(EXPORT TinyXMLTargets NAMESPACE TinyXML::
    DESTINATION lib/cmake/TinyXML
)
