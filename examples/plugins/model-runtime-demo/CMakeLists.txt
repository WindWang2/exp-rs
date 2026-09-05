cmake_minimum_required(VERSION 3.20)
project(model_runtime_demo LANGUAGES CXX)

find_package(ExpRS CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(model_runtime_demo SHARED plugin.cpp)
target_link_libraries(model_runtime_demo PRIVATE ExpRS::SDK)
set_target_properties(model_runtime_demo PROPERTIES OUTPUT_NAME "model_runtime_demo" PREFIX "lib")
