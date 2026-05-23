cmake_minimum_required(VERSION 3.15)
project(raster_ops VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Discover Pybind11 package internally
find_package(pybind11 REQUIRED)
find_package(Eigen3 REQUIRED)

# Compile src/raster_ops.cpp as a native python module
pybind11_add_module(raster_ops src/raster_ops.cpp)

# Link Eigen3
target_link_libraries(raster_ops PRIVATE Eigen3::Eigen)

# Target configurations matching MSVC/GCC
set_target_properties(raster_ops PROPERTIES PREFIX "")
