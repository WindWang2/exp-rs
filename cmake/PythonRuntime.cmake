# Vendored standalone Python and offline wheels. Downloads are an explicit
# preparation step; configure/install refuse absent or stale payloads.
include_guard(GLOBAL)

option(SICNU_BUNDLE_PYTHON "Install the pinned managed Python runtime and offline wheels"
       ${SICNU_ENABLE_PACKAGING})
set(SICNU_PYTHON_RUNTIME_STAGE "${CMAKE_BINARY_DIR}/python-runtime" CACHE PATH
    "Install-prefix tree produced by scripts/prepare_python_runtime.py")

if(SICNU_BUNDLE_PYTHON)
  if(NOT Python_EXECUTABLE)
    find_package(Python REQUIRED COMPONENTS Interpreter)
  endif()
  get_filename_component(_sicnu_python_prepare
    "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_python_runtime.py" ABSOLUTE)
  get_filename_component(_sicnu_python_stage "${SICNU_PYTHON_RUNTIME_STAGE}"
    ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
  execute_process(
    COMMAND "${Python_EXECUTABLE}" "${_sicnu_python_prepare}"
            --verify-stage "${_sicnu_python_stage}"
    RESULT_VARIABLE _sicnu_python_valid
    OUTPUT_VARIABLE _sicnu_python_check_output
    ERROR_VARIABLE _sicnu_python_check_error)
  if(NOT _sicnu_python_valid EQUAL 0)
    message(FATAL_ERROR
      "Vendored Python is not ready: ${_sicnu_python_check_error}\n"
      "Prepare it explicitly before configuring a package:\n"
      "  \"${Python_EXECUTABLE}\" \"${_sicnu_python_prepare}\" --destination \"${_sicnu_python_stage}\"")
  endif()

  # Recheck at install/CPack time too: configure-time success must not allow
  # a later partial, modified or missing runtime into a release archive.
  set(_sicnu_python_install_check [=[
    execute_process(
      COMMAND "@Python_EXECUTABLE@" "@_sicnu_python_prepare@"
              --verify-stage "@_sicnu_python_stage@"
      RESULT_VARIABLE _python_valid
      OUTPUT_VARIABLE _python_output ERROR_VARIABLE _python_error)
    if(NOT _python_valid EQUAL 0)
      message(FATAL_ERROR "Vendored Python install refused: ${_python_error}")
    endif()
  ]=])
  string(CONFIGURE "${_sicnu_python_install_check}" _sicnu_python_install_check @ONLY)
  install(CODE "${_sicnu_python_install_check}" COMPONENT PythonRuntime)
  install(DIRECTORY "${_sicnu_python_stage}/libexec/sicnu-geo-rs/python/"
    DESTINATION libexec/sicnu-geo-rs/python COMPONENT PythonRuntime USE_SOURCE_PERMISSIONS)
  install(DIRECTORY "${_sicnu_python_stage}/share/sicnu-geo-rs/python-wheels/"
    DESTINATION share/sicnu-geo-rs/python-wheels COMPONENT PythonRuntime USE_SOURCE_PERMISSIONS)
  install(FILES "${_sicnu_python_stage}/share/sicnu-geo-rs/python-runtime.json"
    DESTINATION share/sicnu-geo-rs COMPONENT PythonRuntime)
endif()
