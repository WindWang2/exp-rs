# Pinned Node/Pi SDK and the independently staged product wrapper. Preparation
# is explicit: configure/install only verify and never download or run npm.
include_guard(GLOBAL)

option(SICNU_BUNDLE_PI "Install the pinned Pi agent SDK and product wrapper" OFF)
set(SICNU_PI_RUNTIME_STAGE "${CMAKE_BINARY_DIR}/pi-runtime" CACHE PATH
    "Immutable install-prefix tree produced by scripts/prepare_pi_runtime.py")
set(SICNU_PI_PRODUCT_STAGE "${CMAKE_BINARY_DIR}/pi-product" CACHE PATH
    "Small install-prefix tree produced by scripts/prepare_pi_product.py")

if(SICNU_BUNDLE_PI)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _sicnu_pi_processor)
  string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _sicnu_pi_generator_platform)
  if(NOT WIN32 OR CMAKE_CROSSCOMPILING
      OR NOT _sicnu_pi_processor MATCHES "^(amd64|x86_64)$"
      OR (DEFINED CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      OR _sicnu_pi_generator_platform MATCHES "^(win32|arm|arm64)$")
    message(FATAL_ERROR "SICNU_BUNDLE_PI requires native Windows x64; no system Node fallback is supported")
  endif()
  if(NOT Python_EXECUTABLE)
    find_package(Python REQUIRED COMPONENTS Interpreter)
  endif()
  get_filename_component(_sicnu_pi_prepare
    "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_pi_runtime.py" ABSOLUTE)
  get_filename_component(_sicnu_pi_product_prepare
    "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_pi_product.py" ABSOLUTE)
  get_filename_component(_sicnu_pi_stage "${SICNU_PI_RUNTIME_STAGE}"
    ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
  get_filename_component(_sicnu_pi_product_stage "${SICNU_PI_PRODUCT_STAGE}"
    ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")

  execute_process(
    COMMAND "${Python_EXECUTABLE}" -I -B "${_sicnu_pi_prepare}" --verify-stage "${_sicnu_pi_stage}"
    RESULT_VARIABLE _sicnu_pi_valid
    OUTPUT_VARIABLE _sicnu_pi_output ERROR_VARIABLE _sicnu_pi_error TIMEOUT 900)
  if(NOT _sicnu_pi_valid EQUAL 0)
    message(FATAL_ERROR
      "Vendored Pi SDK is not ready: ${_sicnu_pi_error}\n"
      "Prepare it explicitly before configuring:\n"
      "  \"${Python_EXECUTABLE}\" \"${_sicnu_pi_prepare}\" --destination \"${_sicnu_pi_stage}\"")
  endif()
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -I -B "${_sicnu_pi_product_prepare}"
            --verify-stage "${_sicnu_pi_product_stage}" --sdk-stage "${_sicnu_pi_stage}"
    RESULT_VARIABLE _sicnu_pi_product_valid
    OUTPUT_VARIABLE _sicnu_pi_product_output ERROR_VARIABLE _sicnu_pi_product_error TIMEOUT 30)
  if(NOT _sicnu_pi_product_valid EQUAL 0)
    message(FATAL_ERROR
      "Pi product wrapper is not ready: ${_sicnu_pi_product_error}\n"
      "Prepare it explicitly before configuring:\n"
      "  \"${Python_EXECUTABLE}\" \"${_sicnu_pi_product_prepare}\" --destination \"${_sicnu_pi_product_stage}\" --sdk-stage \"${_sicnu_pi_stage}\"")
  endif()

  # The two components have disjoint inventories and preserve the SDK receipt
  # byte for byte. A wrapper edit never triggers SDK preparation or npm ci.
  set(_sicnu_pi_install_check [=[
    execute_process(
      COMMAND [==[@Python_EXECUTABLE@]==] -I -B [==[@_sicnu_pi_prepare@]==]
              --verify-stage [==[@_sicnu_pi_stage@]==]
      RESULT_VARIABLE _pi_valid OUTPUT_VARIABLE _pi_output ERROR_VARIABLE _pi_error TIMEOUT 900)
    if(NOT _pi_valid EQUAL 0)
      message(FATAL_ERROR "Vendored Pi SDK install refused: ${_pi_error}")
    endif()
  ]=])
  string(CONFIGURE "${_sicnu_pi_install_check}" _sicnu_pi_install_check @ONLY)
  install(CODE "${_sicnu_pi_install_check}" COMPONENT PiRuntime)
  foreach(_sicnu_pi_root libexec/sicnu-geo-rs/node libexec/sicnu-geo-rs/pi-agent share/sicnu-geo-rs/pi)
    install(DIRECTORY "${_sicnu_pi_stage}/${_sicnu_pi_root}/"
      DESTINATION "${_sicnu_pi_root}" COMPONENT PiRuntime USE_SOURCE_PERMISSIONS)
  endforeach()
  install(FILES "${_sicnu_pi_stage}/share/sicnu-geo-rs/pi-runtime.json"
    DESTINATION share/sicnu-geo-rs COMPONENT PiRuntime)

  set(_sicnu_pi_product_install_check [=[
    execute_process(
      COMMAND [==[@Python_EXECUTABLE@]==] -I -B [==[@_sicnu_pi_product_prepare@]==]
              --verify-stage [==[@_sicnu_pi_product_stage@]==] --sdk-stage [==[@_sicnu_pi_stage@]==]
      RESULT_VARIABLE _pi_valid OUTPUT_VARIABLE _pi_output ERROR_VARIABLE _pi_error TIMEOUT 30)
    if(NOT _pi_valid EQUAL 0)
      message(FATAL_ERROR "Pi product wrapper install refused: ${_pi_error}")
    endif()
  ]=])
  string(CONFIGURE "${_sicnu_pi_product_install_check}" _sicnu_pi_product_install_check @ONLY)
  install(CODE "${_sicnu_pi_product_install_check}" COMPONENT PiProduct)
  install(DIRECTORY "${_sicnu_pi_product_stage}/libexec/sicnu-geo-rs/pi-product/"
    DESTINATION libexec/sicnu-geo-rs/pi-product COMPONENT PiProduct USE_SOURCE_PERMISSIONS)
  install(FILES "${_sicnu_pi_product_stage}/share/sicnu-geo-rs/pi-product.json"
    DESTINATION share/sicnu-geo-rs COMPONENT PiProduct)

  # Run after both components' copy rules in full install/CPack. Explicit
  # component users install PiRuntime first; PiProduct also verifies the
  # installed SDK, so a component-only wrapper cannot pretend to be complete.
  set(_sicnu_pi_combined_install_check [=[
    execute_process(
      COMMAND [==[@Python_EXECUTABLE@]==] -I -B [==[@_sicnu_pi_product_prepare@]==]
              --verify-install "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}"
      RESULT_VARIABLE _pi_valid OUTPUT_VARIABLE _pi_output ERROR_VARIABLE _pi_error TIMEOUT 900)
    if(NOT _pi_valid EQUAL 0)
      message(FATAL_ERROR "Complete Pi installation refused: ${_pi_error}")
    endif()
  ]=])
  string(CONFIGURE "${_sicnu_pi_combined_install_check}" _sicnu_pi_combined_install_check @ONLY)
  install(CODE "${_sicnu_pi_combined_install_check}" COMPONENT PiProduct)
endif()
