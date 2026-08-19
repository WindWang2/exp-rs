# cmake/sicnu_otb_bundle.cmake — Stage vendored OTB CLI tools for SICNU GEO RS
# NOTE (BUILD-12): sicnu_setup_otb_bundle() is currently not called; bundling is
# done ad hoc via scripts/bundle_otb_tools.sh and scripts/build_with_otb.sh.
# Retained for reference; call explicitly if needed.
#
# Layout (under ${CMAKE_BINARY_DIR}/tools/otb):
#   bin/otbcli, otbcli_<App>, otbApplicationLauncherCommandLine
#   lib/otb/applications/otbapp_*.so
#   otbenv.profile

function(sicnu_setup_otb_bundle)
    set(SICNU_OTB_BUNDLE_DIR "${CMAKE_BINARY_DIR}/tools/otb" CACHE PATH "Bundled OTB tools root")

    set(_bundle_script "${CMAKE_SOURCE_DIR}/scripts/bundle_otb_tools.sh")
    if(NOT EXISTS "${_bundle_script}")
        message(FATAL_ERROR "Missing ${_bundle_script}")
    endif()

    set(_otb_app_groups
        OTBAppSegmentation
        OTBAppFeaturesExtraction
        OTBAppLearning
        OTBAppMiscellaneous
        OTBAppStereo
    )

    add_custom_target(sicnu_otb_bundle
        COMMAND "${_bundle_script}" "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}"
        COMMENT "Bundling OTB CLI tools into ${SICNU_OTB_BUNDLE_DIR}"
        VERBATIM
    )

    # Bundle copies whatever otbcli_* launchers exist in ${CMAKE_BINARY_DIR}/bin.
    # Do not depend on *-all groups — optional apps may fail to build without
    # blocking the main application link step.
    if(TARGET otbApplicationLauncherCommandLine)
        add_dependencies(sicnu_otb_bundle otbApplicationLauncherCommandLine)
    endif()
endfunction()