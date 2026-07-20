# Resolve Boost headers that match installed system Boost runtime libraries.
# Header/library ABI must agree (e.g. both 1.91); mixing vendored 1.84 headers
# with Arch boost-libs 1.91 causes undefined boost::filesystem symbols at link.

function(sicnu_resolve_boost_for_otb out_include_dir out_version)
    find_library(_sicnu_boost_fs NAMES boost_filesystem REQUIRED)
    find_library(_sicnu_boost_ser NAMES boost_serialization REQUIRED)

    get_filename_component(_sicnu_boost_fs_real "${_sicnu_boost_fs}" REALPATH)
    if(_sicnu_boost_fs_real MATCHES "libboost_filesystem\\.so\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(_sicnu_boost_major "${CMAKE_MATCH_1}")
        set(_sicnu_boost_minor "${CMAKE_MATCH_2}")
        set(_sicnu_boost_patch "${CMAKE_MATCH_3}")
    else()
        message(FATAL_ERROR "Cannot parse Boost version from ${_sicnu_boost_fs_real}")
    endif()

    set(_sicnu_boost_ver "${_sicnu_boost_major}.${_sicnu_boost_minor}.${_sicnu_boost_patch}")
    set(_sicnu_boost_lib_tag "${_sicnu_boost_major}_${_sicnu_boost_minor}")

    set(_sicnu_boost_candidates
        "/usr/include"
        "${CMAKE_SOURCE_DIR}/vendor/boost_sys/usr/include"
        "${CMAKE_SOURCE_DIR}/refs/boost"
        "${CMAKE_SOURCE_DIR}/boost_ref"
    )

    set(_sicnu_boost_include "")
    foreach(_sicnu_boost_cand ${_sicnu_boost_candidates})
        if(EXISTS "${_sicnu_boost_cand}/boost/version.hpp")
            file(READ "${_sicnu_boost_cand}/boost/version.hpp" _sicnu_boost_ver_hpp)
            if(_sicnu_boost_ver_hpp MATCHES "#define BOOST_LIB_VERSION \"([0-9]+)_([0-9]+)\"")
                set(_sicnu_hdr_major "${CMAKE_MATCH_1}")
                set(_sicnu_hdr_minor "${CMAKE_MATCH_2}")
                if(_sicnu_hdr_major STREQUAL _sicnu_boost_major AND _sicnu_hdr_minor STREQUAL _sicnu_boost_minor)
                    set(_sicnu_boost_include "${_sicnu_boost_cand}")
                    break()
                endif()
            endif()
        endif()
    endforeach()

    if(NOT _sicnu_boost_include)
        message(FATAL_ERROR
            "Boost headers matching runtime ${_sicnu_boost_ver} not found.\n"
            "  Install dev headers:  pacman -S boost   (Arch)\n"
            "  Or extract to:        vendor/boost_sys/usr/include\n"
            "  Current candidates:   ${_sicnu_boost_candidates}")
    endif()

    set(${out_include_dir} "${_sicnu_boost_include}" PARENT_SCOPE)
    set(${out_version} "${_sicnu_boost_ver}" PARENT_SCOPE)

    set(SICNU_BOOST_FILESYSTEM_LIBRARY "${_sicnu_boost_fs}" PARENT_SCOPE)
    set(SICNU_BOOST_SERIALIZATION_LIBRARY "${_sicnu_boost_ser}" PARENT_SCOPE)

    message(STATUS "Boost for OTB: headers=${_sicnu_boost_include} libs=${_sicnu_boost_ver}")
endfunction()