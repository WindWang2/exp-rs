cmake_minimum_required(VERSION 3.20)
project(sicnu_qt_dependency_abi LANGUAGES CXX)

# Use exactly the SDK selected by the parent CI job. Finding the crypto
# packages alone does not prove that their binaries support that Qt ABI.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Core5Compat)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
set(QT_VERSION_BASE Qt6)
set(QT_VERSION_BASE_LOWER qt6)
find_package(QCA REQUIRED)
find_package(Qt6Keychain REQUIRED)
if(TARGET Qt6Keychain::Qt6Keychain)
    set(keychain_target Qt6Keychain::Qt6Keychain)
else()
    set(keychain_target qt6keychain)
endif()

add_executable(qt_dependency_abi main.cpp)
target_compile_features(qt_dependency_abi PRIVATE cxx_std_17)
set_target_properties(qt_dependency_abi PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(qt_dependency_abi SYSTEM PRIVATE "${QCA_INCLUDE_DIR}")
target_link_libraries(qt_dependency_abi PRIVATE
    "${QCA_LIBRARY}" ${keychain_target}
    Qt6::Core5Compat Qt6::DBus Qt6::Core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(qt_dependency_abi PRIVATE "LINKER:--no-allow-shlib-undefined")
endif()
message(STATUS "Checking Qt ${Qt6_VERSION} ABI against ${QCA_LIBRARY} and ${keychain_target}")
