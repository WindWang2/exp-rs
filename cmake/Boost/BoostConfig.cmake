# Minimal BoostConfig.cmake for OTB integration
# CMake 4.x removed FindBoost.cmake, so we provide this stub.
# Boost headers are at SICNU_BOOST_INCLUDE_DIR, libraries are system-installed.
#
# This file is found via CMAKE_PREFIX_PATH set in the parent CMakeLists.txt.

# Try to find libraries dynamically if they are not already set
if(NOT Boost_FILESYSTEM_LIBRARY)
    find_library(Boost_FILESYSTEM_LIBRARY NAMES boost_filesystem boost_filesystem-mt REQUIRED)
endif()
if(NOT Boost_SERIALIZATION_LIBRARY)
    find_library(Boost_SERIALIZATION_LIBRARY NAMES boost_serialization boost_serialization-mt REQUIRED)
endif()
get_filename_component(Boost_LIBRARY_DIRS "${Boost_FILESYSTEM_LIBRARY}" DIRECTORY)

set(Boost_FOUND TRUE)
set(Boost_INCLUDE_DIRS "${SICNU_BOOST_INCLUDE_DIR}")
set(Boost_LIBRARIES "${Boost_FILESYSTEM_LIBRARY};${Boost_SERIALIZATION_LIBRARY}")
if(NOT Boost_VERSION)
    set(Boost_VERSION "1.91.0")
endif()
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _boost_ver_match "${Boost_VERSION}")
set(Boost_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(Boost_VERSION_MINOR "${CMAKE_MATCH_2}")
set(Boost_VERSION_PATCH "${CMAKE_MATCH_3}")
set(Boost_FILESYSTEM_FOUND TRUE)
set(Boost_SERIALIZATION_FOUND TRUE)
set(Boost_INCLUDE_DIR "${SICNU_BOOST_INCLUDE_DIR}")
if(NOT DEFINED Boost_USE_STATIC_LIBS)
    set(Boost_USE_STATIC_LIBS OFF)
endif()
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
        IMPORTED_LOCATION "${Boost_FILESYSTEM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}")
endif()

if(NOT TARGET Boost::serialization)
    add_library(Boost::serialization SHARED IMPORTED)
    set_target_properties(Boost::serialization PROPERTIES
        IMPORTED_LOCATION "${Boost_SERIALIZATION_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}")
endif()
