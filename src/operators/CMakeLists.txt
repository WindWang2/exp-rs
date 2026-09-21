# SICNU GEO RS — Operator framework and operator implementations

# Leaf seam for the RSOperatorContext/RSOperatorError contracts (#652): both
# sicnu_operators and sicnu_processing used to compile these TUs (two SHARED
# libs exporting the same classes = ODR/RTTI hazard, the #573 class). A leaf
# STATIC lib linked by both keeps exactly one copy. The TUs and their headers
# are dependency-free (std-only), so no Qt/GDAL linkage is needed here.
add_library(sicnu_operators_core STATIC
    framework/rs_operator_context.cpp
    framework/rs_operator_error.cpp
)
# BUILD_INTERFACE keeps in-tree consumption unchanged; INSTALL_INTERFACE makes
# the leaf exportable as part of the ExpRS SDK package (src/sdk/CMakeLists.txt).
target_include_directories(sicnu_operators_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
# rs_operator_error.h (a public header) includes <json/json.h>, so the leaf
# needs the same jsoncpp resolution as its consumers: the canonical jsoncpp
# target when defined, else pkg-config include dirs + libs. Missing this is
# what red-flagged Tier 1/2 with 'json/json.h: No such file or directory'.
if(TARGET jsoncpp)
    target_link_libraries(sicnu_operators_core PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_include_directories(sicnu_operators_core PUBLIC ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(sicnu_operators_core PUBLIC ${JSONCPP_LIBRARIES})
else()
    target_link_libraries(sicnu_operators_core PUBLIC jsoncpp)
endif()
# The leaf is linked into SHARED targets; force PIC so the static objects are
# linkable into sicnu_processing/sicnu_operators regardless of the global
# CMAKE_POSITION_INDEPENDENT_CODE default.
set_target_properties(sicnu_operators_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SICNU_OPERATORS_SOURCES
    framework/rs_progress_callback.cpp
    framework/rs_schema.cpp
    framework/rs_operator.cpp
    framework/rs_operator_registry.cpp
    framework/rs_operation_logger.cpp
    framework/rs_json_params.cpp
    framework/asset_index_pipeline.cpp
    framework/model_catalog.cpp
    framework/artifact_digest.cpp
    framework/chunked_run.cpp
)

# OpenCV-based operators (only when OpenCV imgproc is available)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    list(APPEND SICNU_OPERATORS_SOURCES
        opencv/opencv_utils.cpp
        opencv/opencv_operator_base.cpp
        opencv/opencv_filter_operators.cpp
        opencv/opencv_operators_init.cpp
        # Model runtime layer (cv::dnn tensor surface): session registry,
        # ONNX provider, bounded tile inference engine.
        runtime/model_runtime.cpp
        runtime/tensor_blob.cpp
        runtime/device_planner.cpp
        runtime/nvml_inventory.cpp
        runtime/provider_wire.cpp
        runtime/python_worker_provider.cpp
        runtime/http_provider.cpp
        runtime/opencv_dnn_runtime.cpp
        runtime/onnxruntime_provider.cpp
        runtime/tile_inference_engine.cpp
        runtime/eo_preflight.cpp
        runtime/tensorrt_provider.cpp
        runtime/openvino_provider.cpp
        runtime/detection_postprocess.cpp
        runtime/detection_tile_engine.cpp
        runtime/model_execution_service.cpp
        runtime/model_ensemble.cpp
        runtime/provenance_verify.cpp
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
    otb/otb_bundle_to_perfect_sensor_operator.cpp
    otb/otb_svm_classification_operator.cpp
    otb/otb_compute_images_statistics_operator.cpp
    otb/otb_operators_init.cpp
)
message(STATUS "OTB CLI operators enabled (runtime external tools)")

# GDAL-based operators (direct GDAL C API; always available when GDAL is found)
# Geospatial I/O Foundation 4.0: authoritative conversion/inspection operators
# over the Qt-free sicnu_geospatial core (translate/warp/clip/reproject/
# convert_format/build_overviews/make_cog/vector_convert/inspect/doctor).
list(APPEND SICNU_OPERATORS_SOURCES
    io/io_operators.cpp
    io/io_operators_init.cpp
    io/io_fabric_operators.cpp
    io/io_fabric_operators.h
)
message(STATUS "I/O operators enabled")

list(APPEND SICNU_OPERATORS_SOURCES
    gdal/gdal_operator_utils.cpp
    gdal/gdal_orthorectification_operator.cpp
    gdal/gdal_reproject_operator.cpp
    gdal/gdal_clip_operator.cpp
    gdal/gdal_polygonize_operator.cpp
    gdal/gdal_pansharpen_operator.cpp
    gdal/gdal_operators_init.cpp
)
message(STATUS "GDAL operators enabled")
# Native RS operators (pure C++ algorithms in src/processing/algorithms)
list(APPEND SICNU_OPERATORS_SOURCES
    rs/rs_spectral_index_operator.cpp
    rs/rs_spectral_index_aliases.cpp
    rs/rs_band_math_operator.cpp
    rs/rs_band_tools_operators.cpp
    rs/rs_image_enhancement_operator.cpp
    rs/rs_sam_classify_operator.cpp
    rs/rs_spectral_reference_input.cpp
    rs/rs_spectral_unmixing_operator.cpp
    rs/rs_rx_anomaly_operator.cpp
    rs/rs_local_rx_operator.cpp
    rs/rs_sparse_unmixing_operator.cpp
    rs/rs_spectral_similarity_operator.cpp
    rs/rs_endmember_analysis_operator.cpp
    rs/rs_continuum_removal_operator.cpp
    rs/rs_spectral_resample_operator.cpp
    rs/rs_endmember_extraction_operator.cpp
    rs/rs_atmospheric_correction_operator.cpp
    rs/rs_atmospheric_aliases.cpp
    rs/rs_radiometric_calibration_operator.cpp
    rs/rs_change_detection_operator.cpp
    rs/rs_feature_stack_operator.cpp
    rs/rs_feature_normalize_operator.cpp
    rs/rs_feature_select_operator.cpp
    rs/rs_change_streaming.cpp
    rs/rs_change_primitives.cpp
    rs/rs_threshold_raster_operator.cpp
    rs/rs_sar_calibrate_operator.cpp
    rs/rs_sar_backscatter_operator.cpp
    rs/rs_sar_terrain_flatten_operator.cpp
    rs/rs_sar_terrain_correction_operator.cpp
    rs/rs_sar_speckle_operator.cpp
    rs/rs_sar_ratio_operator.cpp
    rs/rs_sar_texture_operator.cpp
    rs/rs_sar_change_operator.cpp
    rs/rs_post_classification_change_operator.cpp
    rs/rs_image_fusion_operator.cpp
    rs/rs_fusion_aliases.cpp
    rs/rs_qa_mask_operator.cpp
    rs/rs_apply_mask_operator.cpp
    rs/rs_terrain_analysis_operator.cpp
    rs/rs_topographic_correction_operator.cpp
    rs/rs_solar_geometry_operator.cpp
    rs/rs_brdf_normalization_operator.cpp
    rs/rs_radiometric_qa_operator.cpp
    rs/rs_spectral_derivative_operator.cpp
    rs/rs_spectral_detection_operators.cpp
    rs/rs_spectral_spatial_fuse_operator.cpp
    rs/rs_sar_dualpol_operator.cpp
    rs/rs_sar_terrain_masks_operator.cpp
    rs/rs_sar_geocode_operator.cpp
    rs/rs_raster_vector.cpp
    rs/rs_rasterize_operator.cpp
    rs/rs_sar_temporal_stats_operator.cpp
    rs/rs_sar_polsar_decompose_operator.cpp
    rs/rs_sar_interferogram_operator.cpp
    rs/rs_sar_phase_filter_operator.cpp
    rs/rs_sar_unwrap_operator.cpp
    rs/rs_sar_displacement_operator.cpp
    rs/rs_sar_coregister_operator.cpp
    rs/rs_sar_temporal_events_operator.cpp
    rs/rs_sar_remove_topographic_phase_operator.cpp
    rs/rs_sar_coregister_local_operator.cpp
    rs/rs_sar_pair_network_operator.cpp
    rs/rs_sar_network_inversion_operator.cpp
    rs/rs_zonal_stats_operator.cpp
    rs/rs_temporal_monitor_operator.cpp
    rs/rs_terrain_flow_operator.cpp
    rs/rs_terrain_viewshed_operator.cpp
    rs/rs_terrain_solar_operator.cpp
    rs/rs_terrain_landform_operator.cpp
    rs/rs_grid_operators.cpp
    rs/rs_raster_spatial_operators.cpp
    rs/rs_pca_operator.cpp
    rs/rs_mnf_operator.cpp
    rs/rs_mnf_inverse_operator.cpp
    rs/rs_spectral_band_select_operator.cpp
    rs/rs_library_select_operator.cpp
    rs/rs_mosaic_operator.cpp
    # F15 mosaic-fusion-11 (ADR 0163): quality mosaic with seamlines,
    # balancing, blending and provenance.
    rs/rs_quality_mosaic_operator.cpp
    rs/rs_temporal_collection_input.cpp
    rs/rs_temporal_output.cpp
    rs/rs_temporal_summary_operator.cpp
    rs/rs_temporal_composite_operator.cpp
    rs/rs_temporal_index_operator.cpp
    rs/rs_temporal_trend_operator.cpp
    rs/rs_temporal_smooth_operator.cpp
    rs/rs_temporal_gap_fill_operator.cpp
    rs/rs_temporal_harmonic_fit_operator.cpp
    rs/rs_temporal_phenology_operator.cpp
    rs/rs_temporal_breakpoints_operator.cpp
    rs/rs_temporal_sen_trend_operator.cpp
    rs/rs_temporal_sar_fusion_operator.cpp
    rs/rs_temporal_decompose_operator.cpp
    rs/rs_temporal_anomaly_operator.cpp
    rs/rs_temporal_extract_series_operator.cpp
    rs/rs_temporal_extract_regions_operator.cpp
    rs/rs_temporal_regularize_operator.cpp
    rs/rs_temporal_harmonic_breaks_operator.cpp
    rs/rs_temporal_region_features_operator.cpp
    rs/rs_temporal_seasonal_breaks_operator.cpp
    rs/rs_temporal_model_select_operator.cpp
    rs/rs_temporal_phenology_multi_operator.cpp
    rs/rs_landsat_import_operator.cpp
    rs/rs_product_import_plan.cpp
    rs/rs_cn_product_import_operator.cpp
    rs/rs_gaofen_import_operator.cpp
    rs/rs_zy3_import_operator.cpp
    rs/rs_hj_import_operator.cpp
    rs/rs_sentinel2_import_operator.cpp
    rs/rs_modis_import_operator.cpp
    rs/rs_modis_georeference_operator.cpp
    rs/rs_register_images_operator.cpp
    rs/rs_stack_register_operator.cpp
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
        rs/rs_obia_features_operator.cpp
        rs/rs_obia_label_operator.cpp
        rs/rs_obia_common.cpp
        rs/rs_segment_stats_operator.cpp
        rs/rs_segmentation_utils.cpp
        rs/rs_majority_filter_operator.cpp
        rs/rs_recode_operator.cpp
        rs/rs_inference_operator.cpp
        rs/rs_model_task_operators.cpp
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
        sicnu_operators_core
        sicnu_processing
        # io:* operators call the Qt-free foundation directly (Foundation 4.0).
        Sicnu::Geospatial
    PRIVATE
        ${JSONCPP_LIBRARIES}
        Sicnu::data
        # Fault-point probes (Verification Platform 8.0) resolve in the
        # Qt-free leaf sicnu_runtime.
        sicnu_runtime
)

# Platform 7.0: HTTP inference provider (Qt6::Network). When the module is
# absent the provider compiles as a stub and framework "http" surfaces
# runtime_unavailable — the documented graceful-degradation contract.
find_package(Qt6 COMPONENTS Network QUIET)
if(TARGET Qt6::Network)
    target_link_libraries(sicnu_operators PUBLIC Qt6::Network)
    target_compile_definitions(sicnu_operators PRIVATE SICNU_WITH_HTTP_PROVIDER=1)
    message(STATUS "HTTP model provider enabled (Qt6::Network)")
else()
    message(STATUS "HTTP model provider disabled (Qt6::Network not found)")
endif()

# Compiled-in source root so ModelCatalog finds <repo>/models when tests
# run from build dirs and SICNU_MODELS_DIR is unset (CI ctest).
target_compile_definitions(sicnu_operators PUBLIC
  SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# --- Platform 10.0: optional deployment providers (TensorRT / OpenVINO) -----
# Both compile ONLY when explicitly enabled AND the dependency is found; the
# default build configuration is byte-identical to a master build without
# them (the provider TUs degrade to typed-unavailable stubs at compile time).
option(SICNU_ENABLE_TENSORRT "Build the optional TensorRT provider when the SDK is found" OFF)
if(SICNU_ENABLE_TENSORRT)
    find_path(TENSORRT_INCLUDE_DIR NvInfer.h HINTS $ENV{TENSORRT_HOME}/include)
    find_library(TENSORRT_LIBRARY nvinfer HINTS $ENV{TENSORRT_HOME}/lib)
    if(TENSORRT_INCLUDE_DIR AND TENSORRT_LIBRARY)
        target_include_directories(sicnu_operators PRIVATE ${TENSORRT_INCLUDE_DIR})
        target_compile_definitions(sicnu_operators PRIVATE SICNU_WITH_TENSORRT=1)
        target_link_libraries(sicnu_operators PRIVATE ${TENSORRT_LIBRARY})
        message(STATUS "TensorRT model provider enabled (${TENSORRT_LIBRARY})")
    else()
        message(STATUS "TensorRT model provider requested but the SDK was not found")
    endif()
endif()

option(SICNU_ENABLE_OPENVINO "Build the optional OpenVINO provider when the SDK is found" OFF)
if(SICNU_ENABLE_OPENVINO)
    find_path(OpenVINO_INCLUDE_DIR openvino/openvino.hpp HINTS $ENV{OPENVINO_HOME}/runtime/include $ENV{OPENVINO_HOME}/include)
    find_library(OpenVINO_LIBRARY openvino HINTS $ENV{OPENVINO_HOME}/runtime/lib $ENV{OPENVINO_HOME}/lib)
    if(OpenVINO_INCLUDE_DIR AND OpenVINO_LIBRARY)
        target_include_directories(sicnu_operators PRIVATE ${OpenVINO_INCLUDE_DIR})
        target_compile_definitions(sicnu_operators PRIVATE SICNU_WITH_OPENVINO=1)
        target_link_libraries(sicnu_operators PRIVATE ${OpenVINO_LIBRARY})
        message(STATUS "OpenVINO model provider enabled (${OpenVINO_LIBRARY})")
    else()
        message(STATUS "OpenVINO model provider requested but the SDK was not found")
    endif()
endif()

if(SICNU_WITH_ONNX_RUNTIME AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_include_directories(sicnu_operators PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime ${ONNXRUNTIME_INCLUDE_DIR})
    target_compile_definitions(sicnu_operators PRIVATE SICNU_WITH_ONNX_RUNTIME=1)
    target_link_libraries(sicnu_operators PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()

if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    target_include_directories(sicnu_operators PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(sicnu_operators PRIVATE
        ${OpenCV_LIBRARIES}
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
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

install(TARGETS sicnu_operators
    RUNTIME DESTINATION ${QGIS_BIN_DIR}
    LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
    ARCHIVE DESTINATION ${QGIS_LIB_DIR}
)

# ---------------------------------------------------------------------------
# Python bindings via pybind11 (optional, requires Python dev + pybind11)
# ---------------------------------------------------------------------------
set(SICNU_BUILD_PYTHON_BINDINGS FALSE)
# SICNU_LAB_PROFILE (D10): the pybind11 FetchContent fallback is a network
# dependency teaching machines cannot satisfy — bindings stay off there.
if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND
   AND NOT SICNU_LAB_SKIP_PYTHON_BINDINGS)
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
            CXX_SCAN_FOR_MODULES OFF
        )
        install(TARGETS _sicnu_operators
            RUNTIME DESTINATION ${QGIS_BIN_DIR}
            LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
            ARCHIVE DESTINATION ${QGIS_LIB_DIR}
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
