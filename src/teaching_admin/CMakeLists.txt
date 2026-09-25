# Teacher Authoring & Assessment Console — Qt Core orchestration leaf.
# Calls existing curriculum / LabSpec / pack / grader / offline / batch seams.
# Owns NO second truth. Forbidden: Widgets, Network, qgis_gui, Sicnu::app.

qt_add_library(sicnu_teaching_admin STATIC
  admin_types.h
  admin_errors.h
  json_util.h
  curriculum_editor.cpp
  curriculum_editor.h
  labspec_authoring.cpp
  labspec_authoring.h
  rubric_builder.cpp
  rubric_builder.h
  data_pack_manager.cpp
  data_pack_manager.h
  script_adapters.cpp
  script_adapters.h
  grader_cli_adapter.cpp
  grader_cli_adapter.h
  operator_catalog.cpp
  operator_catalog.h
  student_projection.cpp
  student_projection.h
  release_preflight.cpp
  release_preflight.h
  batch_assessment.cpp
  batch_assessment.h
  feedback_pack.cpp
  feedback_pack.h
  class_summary.cpp
  class_summary.h
)

add_library(Sicnu::teaching_admin ALIAS sicnu_teaching_admin)

target_include_directories(sicnu_teaching_admin
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_teaching_admin
  PUBLIC
    Qt6::Core
    # sicnu.lab-pack/1: pack validation delegates to the Qt-free shared leaf
    # (ONE parser/validator shared with sicnu::agent) — no second pack truth.
    sicnu_lab_pack
)

foreach(SICNU_TA_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network Sicnu::app)
  get_target_property(SICNU_TA_DIRECT_LINKS sicnu_teaching_admin LINK_LIBRARIES)
  if(SICNU_TA_FORBIDDEN_LINK IN_LIST SICNU_TA_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_teaching_admin must not depend on forbidden target ${SICNU_TA_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_teaching_admin PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC OFF
)
