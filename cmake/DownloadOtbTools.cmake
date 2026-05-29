# cmake/DownloadOtbTools.cmake
include(FetchContent)

set(OTB_VERSION "9.1.0" CACHE STRING "OTB tools version")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OTB_URL "https://www.orfeo-toolbox.org/packages/OTB-${OTB_VERSION}-Linux64.tar.gz")
else()
    message(WARNING "OTB tools download not supported on this platform")
    return()
endif()

FetchContent_Declare(
    otb_tools
    URL ${OTB_URL}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/otb
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

FetchContent_MakeAvailable(otb_tools)

install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/otb/bin/
        DESTINATION tools/otb
        USE_SOURCE_PERMISSIONS
        FILES_MATCHING
        PATTERN "otbcli_*")
