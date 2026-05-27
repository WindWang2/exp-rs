find_package(pybind11 REQUIRED HINTS /home/kevin/.local/lib/python3.13/site-packages/pybind11/share/cmake/pybind11)
pybind11_add_module(_antigravity_core
  ${CMAKE_SOURCE_DIR}/src/python/bindings.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(_antigravity_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/gui
  ${CMAKE_SOURCE_DIR}/src/runtime
  ${CMAKE_BINARY_DIR})
target_link_libraries(_antigravity_core PRIVATE qgis_core qgis_gui Qt6::Core Qt6::Gui Qt6::Widgets)
