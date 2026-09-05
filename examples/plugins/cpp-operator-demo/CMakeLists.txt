cmake_minimum_required(VERSION 3.20)
project(cpp_operator_demo LANGUAGES CXX)

# Third-party plugin builds consume the installed ExpRS SDK exactly like any
# other CMake package. Nothing from the exp-rs source tree is required.
find_package(ExpRS CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep plugin manifests honest: ExpRS_PLUGIN_API_VERSION / ExpRS_PLUGIN_ABI_VERSION
# are exported by the package config for embedding into the manifest.
add_library(cpp_operator_demo SHARED
  plugin.cpp
)

# ExpRS::SDK carries: include/exprs + include/operators/framework headers,
# the sicnu_operators_core link leaf and jsoncpp.
target_link_libraries(cpp_operator_demo PRIVATE ExpRS::SDK)

# The manifest entrypoint must match the library output name exactly.
set_target_properties(cpp_operator_demo PROPERTIES
  OUTPUT_NAME "cpp_operator_demo"
  PREFIX "lib"
)

message(STATUS "ExpRS plugin API ${ExpRS_PLUGIN_API_VERSION} (ABI ${ExpRS_PLUGIN_ABI_VERSION})")
