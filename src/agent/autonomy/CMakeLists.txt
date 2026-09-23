# src/agent/autonomy/CMakeLists.txt
#
# RS14-12 teaching autonomy ladder: the L0..L5 policy layer.
#
# sicnu_autonomy is a Qt-free, QGIS-free leaf over jsoncpp only: policy
# schema, level semantics, capability taxonomy, decision engine, bounded
# audit log, and status projection. Keeping it linkable without the GUI chain
# lets the whole policy surface be tested by light suites (Catch2 + this
# library), and the two gate call sites in sicnu_agent stay thin.
#
# Single-source-of-truth note: risk classes (tool_manifest) and lab roles
# (harness_actions) live in sicnu_agent and are passed IN as request fields;
# this library never re-declares them.

add_library(sicnu_autonomy STATIC
  autonomy_level.cpp
  autonomy_level.h
  autonomy_capability.cpp
  autonomy_capability.h
  autonomy_classification.cpp
  autonomy_classification.h
  autonomy_policy.cpp
  autonomy_policy.h
  autonomy_decision.cpp
  autonomy_decision.h
  autonomy_audit.cpp
  autonomy_audit.h
  autonomy_projection.cpp
  autonomy_projection.h
  autonomy_holder.cpp
  autonomy_holder.h
  fake_action_provider.h
)

add_library(Sicnu::autonomy ALIAS sicnu_autonomy)

target_include_directories(sicnu_autonomy
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Root CMakeLists already resolves jsoncpp → jsoncpp_lib alias. Re-running
# find_package(jsoncpp) on Homebrew recreates imported target jsoncpp_lib and
# fails configure ("another target with the same name already exists").
if(NOT TARGET jsoncpp AND NOT TARGET jsoncpp_lib)
  find_package(jsoncpp QUIET)
endif()
if(TARGET jsoncpp)
  target_link_libraries(sicnu_autonomy PUBLIC jsoncpp)
elseif(TARGET jsoncpp_lib)
  target_link_libraries(sicnu_autonomy PUBLIC jsoncpp_lib)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_autonomy PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_autonomy PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_autonomy PUBLIC jsoncpp)
endif()

set_target_properties(sicnu_autonomy PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
