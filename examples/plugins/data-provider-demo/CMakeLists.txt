cmake_minimum_required(VERSION 3.20)
project(data_provider_demo LANGUAGES CXX)

find_package(ExpRS CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(data_provider_demo SHARED plugin.cpp)
target_link_libraries(data_provider_demo PRIVATE ExpRS::SDK)
set_target_properties(data_provider_demo PROPERTIES OUTPUT_NAME "data_provider_demo" PREFIX "lib")
