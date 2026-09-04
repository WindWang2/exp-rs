cmake_minimum_required(VERSION 3.20)
project(ui_dock_demo LANGUAGES CXX)

find_package(ExpRS CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(ui_dock_demo SHARED plugin.cpp)
target_link_libraries(ui_dock_demo PRIVATE ExpRS::SDK Qt6::Widgets)
set_target_properties(ui_dock_demo PROPERTIES OUTPUT_NAME "ui_dock_demo" PREFIX "lib")
