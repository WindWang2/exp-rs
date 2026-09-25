# SicnuSharedLinkGuard — fail a SHARED sicnu_* library at ITS OWN link step
# when it references a symbol none of its link dependencies provide.
#
# Why: on ELF, `ld` happily produces a .so with undefined symbols and defers
# the error to every downstream executable. In the 1750fa93 review,
# libsicnu_agent.so referenced agent_loop::VerificationReport::aggregate
# without linking sicnu_agent_loop and ~161 test executables failed to link
# instead of one library. MSVC (DLLs) and Apple ld (-undefined error) already
# behave this way, so this only aligns Linux GCC/Clang with them.
#
# Skipped when:
#   * not Linux/ELF (MSVC/Apple already strict; MinGW/Cygwin untested),
#   * ENABLE_SANITIZERS=ON — the sanitizer runtime is linked into
#     executables only (see top-level CMakeLists), so instrumented shared
#     libraries legitimately carry undefined __asan_*/__ubsan_* references,
#   * SICNU_SHARED_NO_UNDEFINED=OFF (escape hatch).
#
# Scope: only project-owned SHARED libraries named sicnu_* under src/.
# MODULE libraries and the legacy in-process plugins (layer_tree_plugin,
# processing_plugin), which resolve host symbols at dlopen time, are left
# alone.

option(SICNU_SHARED_NO_UNDEFINED
  "Link sicnu_* shared libraries with --no-undefined on Linux (GCC/Clang)" ON)

function(_sicnu_collect_targets_recursive dir out_var)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_sub IN LISTS _subdirs)
    _sicnu_collect_targets_recursive("${_sub}" _sub_targets)
    list(APPEND _targets ${_sub_targets})
  endforeach()
  set(${out_var} ${_targets} PARENT_SCOPE)
endfunction()

function(sicnu_apply_shared_link_guard)
  if(NOT SICNU_SHARED_NO_UNDEFINED)
    return()
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    return()
  endif()
  if(ENABLE_SANITIZERS)
    message(STATUS "SicnuSharedLinkGuard: skipped (ENABLE_SANITIZERS=ON)")
    return()
  endif()

  _sicnu_collect_targets_recursive("${CMAKE_SOURCE_DIR}" _all)
  set(_guarded "")
  foreach(_t IN LISTS _all)
    if(NOT _t MATCHES "^sicnu_")
      continue()
    endif()
    get_target_property(_srcdir ${_t} SOURCE_DIR)
    string(FIND "${_srcdir}/" "${CMAKE_SOURCE_DIR}/src/" _pos)
    if(NOT _pos EQUAL 0)
      continue()
    endif()
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "SHARED_LIBRARY")
      target_link_options(${_t} PRIVATE "LINKER:--no-undefined")
      list(APPEND _guarded ${_t})
    endif()
  endforeach()
  list(JOIN _guarded ", " _guarded_str)
  message(STATUS "SicnuSharedLinkGuard: --no-undefined on ${_guarded_str}")
endfunction()
