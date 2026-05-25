add_executable(test_render_smoke test_render_smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(test_render_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/runtime
  ${CMAKE_BINARY_DIR})
target_link_libraries(test_render_smoke PRIVATE qgis_core Qt6::Core Qt6::Gui)
add_test(NAME render_smoke COMMAND test_render_smoke)

add_executable(test_thread_safety test_thread_safety.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/antigravity_init.cpp)
target_include_directories(test_thread_safety PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/runtime
  ${CMAKE_BINARY_DIR})
target_link_libraries(test_thread_safety PRIVATE qgis_core Qt6::Core Qt6::Concurrent)
add_test(NAME thread_safety COMMAND test_thread_safety)
