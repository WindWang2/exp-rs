# Vendored MuParserX for OTB FeaturesExtraction (BandMath, Smoothing, etc.)
cmake_minimum_required(VERSION 3.13)
project(muparserx_vendored LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB MUPARSERX_VENDORED_SOURCES "${MUPARSERX_SOURCE_DIR}/parser/*.cpp")

add_library(muparserx SHARED ${MUPARSERX_VENDORED_SOURCES})
target_include_directories(muparserx PUBLIC
    $<BUILD_INTERFACE:${MUPARSERX_SOURCE_DIR}/parser>
    $<INSTALL_INTERFACE:include/muparserx>
)
set_target_properties(muparserx PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

find_library(M_LIBRARY NAMES m)
if(M_LIBRARY)
    target_link_libraries(muparserx PRIVATE ${M_LIBRARY})
endif()

install(TARGETS muparserx EXPORT MuParserXTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)