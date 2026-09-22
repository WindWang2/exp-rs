# Study → ExecutionPlane adapter (RS14-07).
#
# Thin bridge leaf (pattern: sicnu_experiment_bridge): routes study points
# through the single execution spine and commits outputs to the study-owned
# run directory. Deliberately NOT part of sicnu_study (the science core must
# not link the execution stack); consumers are opt-in surfaces only.
qt_add_library(sicnu_study_bridge STATIC
  study_execution_plane.cpp
  study_execution_plane.h
)

add_library(Sicnu::study_bridge ALIAS sicnu_study_bridge)

target_include_directories(sicnu_study_bridge
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_study_bridge
  PUBLIC
    Qt6::Core
    Sicnu::study
    sicnu_task_center
)

# jsoncpp is used in the adapter body only (committed-payload conversion); the
# canonical `jsoncpp` target is resolved once at the top level (see
# sicnu_processing for the resolution rationale).
if(TARGET jsoncpp)
  target_link_libraries(sicnu_study_bridge PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_study_bridge PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_study_bridge PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_study_bridge PRIVATE jsoncpp)
endif()

set_target_properties(sicnu_study_bridge PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

# Same science-core guard as sicnu_study: the adapter projects execution truth
# into study records; it never gains GUI or network behavior.
foreach(SICNU_STUDY_BRIDGE_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_STUDY_BRIDGE_DIRECT_LINKS sicnu_study_bridge LINK_LIBRARIES)
  if(SICNU_STUDY_BRIDGE_FORBIDDEN_LINK IN_LIST SICNU_STUDY_BRIDGE_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_study_bridge must not depend on forbidden target ${SICNU_STUDY_BRIDGE_FORBIDDEN_LINK}"
    )
  endif()
endforeach()
