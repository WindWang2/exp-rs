# Minimal BoostConfig.cmake for OTB integration
# CMake 4.x removed FindBoost.cmake, so we provide this stub.
# Boost headers are at SICNU_BOOST_INCLUDE_DIR, libraries are system-installed.
#
# This file is found via CMAKE_PREFIX_PATH set in the parent CMakeLists.txt.

set(Boost_FOUND TRUE)
set(Boost_INCLUDE_DIRS "${SICNU_BOOST_INCLUDE_DIR}")
set(Boost_LIBRARIES "/usr/lib/libboost_filesystem.so;/usr/lib/libboost_serialization.so")
set(Boost_LIBRARY_DIRS "/usr/lib")
set(Boost_VERSION "1.84.0")
set(Boost_VERSION_MAJOR 1)
set(Boost_VERSION_MINOR 84)
set(Boost_VERSION_PATCH 0)
set(Boost_FILESYSTEM_FOUND TRUE)
set(Boost_SERIALIZATION_FOUND TRUE)
set(Boost_INCLUDE_DIR "${SICNU_BOOST_INCLUDE_DIR}")
set(Boost_FILESYSTEM_LIBRARY "/usr/lib/libboost_filesystem.so")
set(Boost_SERIALIZATION_LIBRARY "/usr/lib/libboost_serialization.so")
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)

# Create imported targets that modern find_package expects
if(NOT TARGET Boost::boost)
    add_library(Boost::boost INTERFACE IMPORTED)
    set_target_properties(Boost::boost PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}")
endif()

if(NOT TARGET Boost::filesystem)
    add_library(Boost::filesystem SHARED IMPORTED)
    set_target_properties(Boost::filesystem PROPERTIES
        IMPORTED_LOCATION "/usr/lib/libboost_filesystem.so"
        INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}")
endif()

if(NOT TARGET Boost::serialization)
    add_library(Boost::serialization SHARED IMPORTED)
    set_target_properties(Boost::serialization PROPERTIES
        IMPORTED_LOCATION "/usr/lib/libboost_serialization.so"
        INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}")
endif()
