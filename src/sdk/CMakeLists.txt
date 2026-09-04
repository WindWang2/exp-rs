###############################################################################
# src/sdk/CMakeLists.txt — ExpRS Developer Platform public SDK
#
# In-tree:
#   sicnu_sdk    STATIC library hosting the plugin infrastructure (manifest,
#                discovery, registry, loader, diagnostics, external process).
#                Used by the host application/CLI and the tests.
#   exprs_sdk    INTERFACE target = the plugin-facing API surface (headers +
#                sicnu_operators_core + jsoncpp). In-tree equivalents of the
#                installed ExpRS::SDK package target.
#
# Installed (component `sdk`, or default install):
#   include/exprs/*.h                     public SDK headers
#   include/operators/framework/*.h       operator contract headers
#   lib/libsicnu_operators_core.a         plugin link leaf (static)
#   lib/cmake/ExpRS/*                     find_package(ExpRS) package
#   share/exp-rs/                         schemas & shared resources
###############################################################################

set( EXPRS_SDK_SOURCES
  exprs/plugin_diagnostics.cpp
  exprs/plugin_manifest.cpp
  exprs/plugin_permissions.cpp
  exprs/plugin_validator.cpp
  exprs/plugin_record.cpp
  exprs/plugin_discovery.cpp
  exprs/plugin_loader.cpp
  exprs/plugin_registry.cpp
  exprs/plugin_package.cpp
  exprs/external_process.cpp
  exprs/workflow_schema.cpp
  exprs/workflow_builder.cpp
)

add_library( sicnu_sdk STATIC ${EXPRS_SDK_SOURCES} )
target_include_directories( sicnu_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/sdk>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries( sicnu_sdk
  PUBLIC
    jsoncpp
)
target_compile_features( sicnu_sdk PUBLIC cxx_std_20 )

# The plugin-facing API surface. Plugin projects in-tree (tests) link this;
# out-of-tree projects link the installed ExpRS::SDK with identical contents.
add_library( exprs_sdk INTERFACE )
target_include_directories( exprs_sdk
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/sdk>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries( exprs_sdk INTERFACE sicnu_operators_core jsoncpp )
target_compile_features( exprs_sdk INTERFACE cxx_std_20 )
add_library( ExpRS::SDK ALIAS exprs_sdk )

###############################################################################
# Install + export
###############################################################################

set( EXPRS_SDK_INCLUDE_HEADERS
  exprs/version.h
  exprs/plugin_diagnostics.h
  exprs/plugin_manifest.h
  exprs/plugin_permissions.h
  exprs/plugin_validator.h
  exprs/plugin_record.h
  exprs/plugin_discovery.h
  exprs/plugin_loader.h
  exprs/plugin_registry.h
  exprs/plugin_interface.h
  exprs/plugin_package.h
  exprs/external_process.h
  exprs/plugin_ui.h
  exprs/exit_codes.h
  exprs/workflow_schema.h
  exprs/workflow_builder.h
)

# Operator contract headers required by plugin authors (self-contained
# subset of operators/framework — jsoncpp + std only).
set( EXPRS_OPERATOR_CONTRACT_HEADERS
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_operator.h
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_operator_context.h
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_operator_error.h
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_schema.h
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_progress_callback.h
  ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_json_params.h
)

install( FILES ${EXPRS_SDK_INCLUDE_HEADERS}
  DESTINATION include/exprs
  COMPONENT sdk
)
install( FILES ${EXPRS_OPERATOR_CONTRACT_HEADERS}
  DESTINATION include/operators/framework
  COMPONENT sdk
)

install( TARGETS sicnu_sdk exprs_sdk
  EXPORT ExpRSTargets
  ARCHIVE DESTINATION lib COMPONENT sdk
  LIBRARY DESTINATION lib COMPONENT sdk
  INCLUDES DESTINATION include
)

# The operator contract link leaf: plugin projects link this static archive
# so RSOperatorContext/RSOperatorError symbols resolve without dragging the
# full operators library into a plugin build.
install( TARGETS sicnu_operators_core
  EXPORT ExpRSTargets
  ARCHIVE DESTINATION lib COMPONENT sdk
  LIBRARY DESTINATION lib COMPONENT sdk
)

install( EXPORT ExpRSTargets
  FILE ExpRSTargets.cmake
  NAMESPACE ExpRS::
  DESTINATION lib/cmake/ExpRS
  COMPONENT sdk
)

# Package configuration ------------------------------------------------------
include( CMakePackageConfigHelpers )

# The SDK package version follows the plugin API (exprs/version.h), not the
# application version. Keep the two values in sync with exprs/version.h.
set( EXP_RS_PACKAGE_VERSION 3.0.0 )
set( EXP_RS_PLUGIN_API_VERSION "3.0" )
set( EXP_RS_PLUGIN_ABI_VERSION 1 )

configure_package_config_file(
  ${CMAKE_SOURCE_DIR}/cmake/ExpRSConfig.cmake.in
  ${CMAKE_BINARY_DIR}/cmake/ExpRSConfig.cmake
  INSTALL_DESTINATION lib/cmake/ExpRS
)
write_basic_package_version_file(
  ${CMAKE_BINARY_DIR}/cmake/ExpRSConfigVersion.cmake
  VERSION ${EXP_RS_PACKAGE_VERSION}
  COMPATIBILITY SameMajorVersion
)
install( FILES
  ${CMAKE_BINARY_DIR}/cmake/ExpRSConfig.cmake
  ${CMAKE_BINARY_DIR}/cmake/ExpRSConfigVersion.cmake
  DESTINATION lib/cmake/ExpRS
  COMPONENT sdk
)

# Shared schemas shipped with the SDK.
install( DIRECTORY ${CMAKE_SOURCE_DIR}/docs/schemas/
  DESTINATION share/exp-rs/schemas
  COMPONENT sdk
  FILES_MATCHING PATTERN "*.json"
)
