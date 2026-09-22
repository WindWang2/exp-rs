# Undergraduate Lab Cockpit — projection leaf (RS Teaching).
#
# sicnu_teaching projects curriculum / lab / autonomy / verifier / grader
# documents into student-facing value models. It owns NO second truth.
# Qt-free (jsoncpp + Sicnu::autonomy only) so Catch2 suites stay light.

add_library(sicnu_teaching STATIC
  lab_status.cpp
  lab_status.h
  course_home_view_model.cpp
  course_home_view_model.h
  lab_step_timeline.cpp
  lab_step_timeline.h
  lab_readiness.cpp
  lab_readiness.h
  autonomy_effective_display.cpp
  autonomy_effective_display.h
  lab_feedback_projection.cpp
  lab_feedback_projection.h
  lab_session_state.cpp
  lab_session_state.h
)

add_library(Sicnu::teaching ALIAS sicnu_teaching)

target_include_directories(sicnu_teaching
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_teaching
  PUBLIC
    Sicnu::autonomy
)

if(TARGET jsoncpp)
  target_link_libraries(sicnu_teaching PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_teaching PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_teaching PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_teaching PUBLIC jsoncpp)
endif()

foreach(SICNU_TEACHING_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network Sicnu::app)
  get_target_property(SICNU_TEACHING_DIRECT_LINKS sicnu_teaching LINK_LIBRARIES)
  if(SICNU_TEACHING_FORBIDDEN_LINK IN_LIST SICNU_TEACHING_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_teaching must not depend on forbidden target ${SICNU_TEACHING_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_teaching PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
