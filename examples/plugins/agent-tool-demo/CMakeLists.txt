cmake_minimum_required(VERSION 3.20)
project(agent_tool_demo LANGUAGES CXX)

find_package(ExpRS CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(agent_tool_demo SHARED plugin.cpp)
target_link_libraries(agent_tool_demo PRIVATE ExpRS::SDK)
set_target_properties(agent_tool_demo PROPERTIES OUTPUT_NAME "agent_tool_demo" PREFIX "lib")
