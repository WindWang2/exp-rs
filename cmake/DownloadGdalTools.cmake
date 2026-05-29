# cmake/DownloadGdalTools.cmake
include(FetchContent)

set(GDAL_TOOLS_VERSION "3.8.0" CACHE STRING "GDAL tools version")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GDAL_TOOLS_URL "https://github.com/OSGeo/gdal/releases/download/v${GDAL_TOOLS_VERSION}/gdal-${GDAL_TOOLS_VERSION}-linux-x86_64.tar.gz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(GDAL_TOOLS_URL "https://github.com/OSGeo/gdal/releases/download/v${GDAL_TOOLS_VERSION}/gdal-${GDAL_TOOLS_VERSION}-macosx-arm64.tar.gz")
else()
    message(WARNING "GDAL tools download not supported on this platform")
    return()
endif()

FetchContent_Declare(
    gdal_tools
    URL ${GDAL_TOOLS_URL}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/gdal
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

FetchContent_MakeAvailable(gdal_tools)

install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/gdal/bin/
        DESTINATION tools/gdal
        USE_SOURCE_PERMISSIONS
        FILES_MATCHING
        PATTERN "gdal*"
        PATTERN "ogr*"
        PATTERN "ogr2ogr"
        PATTERN "ogrtindex")
