# SICNU GEO RS — Operator framework and operator implementations
set(SICNU_OPERATORS_SOURCES
    framework/rs_operator_error.cpp
    framework/rs_progress_callback.cpp
    framework/rs_operator_context.cpp
    framework/rs_schema.cpp
    framework/rs_operator.cpp
    framework/rs_operator_registry.cpp
    framework/rs_operation_logger.cpp
    framework/rs_json_params.cpp
    framework/asset_index_pipeline.cpp
)

# OpenCV-based operators (only when OpenCV imgproc is available)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    list(APPEND SICNU_OPERATORS_SOURCES
        opencv/opencv_utils.cpp
        opencv/opencv_operator_base.cpp
        opencv/opencv_filter_operators.cpp
        opencv/opencv_operators_init.cpp
    )
    message(STATUS "OpenCV operators enabled (imgproc found)")
else()
    message(STATUS "OpenCV operators disabled (imgproc not found)")
endif()

# OTB CLI-based operators (do not require OTB headers/libs at compile time;
# they locate and execute external otbcli_* binaries at runtime).
list(APPEND SICNU_OPERATORS_SOURCES
    otb/otb_operator_base.cpp
    otb/otb_segmentation_operator.cpp
    otb/otb_svm_classification_operator.cpp
    otb/otb_compute_images_statistics_operator.cpp
    otb/otb_operators_init.cpp
)
message(STATUS "OTB CLI operators enabled (runtime external tools)")

# GDAL-based operators (direct GDAL C API; always available when GDAL is found)
list(APPEND SICNU_OPERATORS_SOURCES
    gdal/gdal_operator_utils.cpp
    gdal/gdal_orthorectification_operator.cpp
    gdal/gdal_reproject_operator.cpp
    gdal/gdal_clip_operator.cpp
    gdal/gdal_polygonize_operator.cpp
    gdal/gdal_operators_init.cpp
)
message(STATUS "GDAL operators enabled")
# Native RS operators (pure C++ algorithms in src/processing/algorithms)
list(APPEND SICNU_OPERATORS_SOURCES
    rs/rs_spectral_index_operator.cpp
    rs/rs_band_math_operator.cpp
    rs/rs_atmospheric_correction_operator.cpp
    rs/rs_change_detection_operator.cpp
    rs/rs_image_fusion_operator.cpp
    rs/rs_terrain_analysis_operator.cpp
    rs/rs_pca_operator.cpp
    rs/rs_mosaic_operator.cpp
    rs/rs_landsat_import_operator.cpp
    rs/rs_sentinel2_import_operator.cpp
    rs/rs_modis_import_operator.cpp
    rs/rs_modis_georeference_operator.cpp
    rs/rs_operators_init.cpp
)

# Classification operators use OpenCV (core + ml when available)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
    list(APPEND SICNU_OPERATORS_SOURCES
        rs/rs_kmeans_operator.cpp
        rs/rs_supervised_classification_operator.cpp
        rs/rs_obia_segment_operator.cpp
        rs/rs_obia_classify_operator.cpp
        rs/rs_obia_hierarchy_operator.cpp
        rs/rs_segment_stats_operator.cpp
        rs/rs_segmentation_utils.cpp
    )
    message(STATUS "RS classification/OBIA operators enabled")
endif()

message(STATUS "Native RS operators enabled")

add_library(sicnu_operators SHARED
    ${SICNU_OPERATORS_SOURCES}
)

target_include_directories(sicnu_operators
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${JSONCPP_INCLUDE_DIRS}
)

target_link_libraries(sicnu_operators
    PUBLIC
        Qt6::Core
        sicnu_processing
    PRIVATE
        ${JSONCPP_LIBRARIES}
        Sicnu::data
)

if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    target_include_directories(sicnu_operators PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(sicnu_operators PRIVATE
        ${OpenCV_LIBS}
    )
    target_compile_definitions(sicnu_operators PUBLIC SICNU_HAS_OPENCV=1)
endif()

if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND TARGET qgis_analysis)
    target_link_libraries(sicnu_operators PUBLIC qgis_analysis)
    target_include_directories(sicnu_operators PUBLIC
        ${CMAKE_SOURCE_DIR}/src/analysis
        ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
        ${CMAKE_SOURCE_DIR}/src/analysis/classification
    )
endif()

# jsoncpp headers may be in a system path that requires -I flags from pkg-config
if(JSONCPP_CFLAGS_OTHER)
    target_compile_options(sicnu_operators PRIVATE ${JSONCPP_CFLAGS_OTHER})
endif()

set_target_properties(sicnu_operators PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS sicnu_operators
    LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
)

# ---------------------------------------------------------------------------
# Python bindings via pybind11 (optional, requires Python dev + pybind11)
# ---------------------------------------------------------------------------
set(SICNU_BUILD_PYTHON_BINDINGS FALSE)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    find_package(Python COMPONENTS Development QUIET)
    if(Python_Development_FOUND)
        find_package(pybind11 QUIET)
        if(NOT pybind11_FOUND)
            include(FetchContent)
            FetchContent_Declare(
                pybind11
                GIT_REPOSITORY https://github.com/pybind/pybind11.git
                GIT_TAG        v2.13.6
            )
            FetchContent_MakeAvailable(pybind11)
        endif()

        pybind11_add_module(_sicnu_operators python/pybind_sicnu_operators.cpp)
        target_include_directories(_sicnu_operators
            PUBLIC
                ${CMAKE_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
        target_link_libraries(_sicnu_operators PRIVATE
            sicnu_operators
            ${JSONCPP_LIBRARIES}
        )
        if(JSONCPP_CFLAGS_OTHER)
            target_compile_options(_sicnu_operators PRIVATE ${JSONCPP_CFLAGS_OTHER})
        endif()
        set_target_properties(_sicnu_operators PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
        )
        install(TARGETS _sicnu_operators
            LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
        )

        # Stage the pure-Python package wrapper so `import sicnu_operators` works.
        set(_sicnu_python_pkg_src "${CMAKE_CURRENT_SOURCE_DIR}/python/sicnu_operators")
        set(_sicnu_python_pkg_dst "${CMAKE_BINARY_DIR}/lib/sicnu_operators")
        add_custom_command(TARGET _sicnu_operators POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${_sicnu_python_pkg_src} ${_sicnu_python_pkg_dst}
            COMMENT "Staging sicnu_operators Python package"
        )
        install(DIRECTORY ${_sicnu_python_pkg_src}
            DESTINATION ${QGIS_LIB_SUBDIR}
        )

        set(SICNU_BUILD_PYTHON_BINDINGS TRUE PARENT_SCOPE)
        set(SICNU_BUILD_PYTHON_BINDINGS TRUE CACHE BOOL "Python bindings were built" FORCE)
        message(STATUS "Python bindings enabled (pybind11 + cv::Mat/ndarray zero-copy)")
    else()
        message(STATUS "Python Development not found, skipping Python bindings")
    endif()
else()
    message(STATUS "OpenCV not found, skipping Python bindings")
endif()
