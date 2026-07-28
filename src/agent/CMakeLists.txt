# src/agent/CMakeLists.txt
add_library(sicnu_agent SHARED
  llm_config_manager.cpp
  llm_config_manager.h
  llm_streaming_client.cpp
  llm_streaming_client.h
)

target_link_libraries(sicnu_agent PUBLIC
  Qt6::Core
  Qt6::Network
  sicnu_processing
)

target_include_directories(sicnu_agent PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)
