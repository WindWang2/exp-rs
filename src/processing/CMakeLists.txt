add_library(sicnu_processing SHARED
    algorithm_help_catalog.cpp
    tools/tool_path_manager.cpp
    tools/cli_tool_discovery.cpp

    gdal/gdal_dataset_wrapper.cpp
    gdal/gdal_async_reader.cpp
    gdal/gdal_block_stream.cpp
    gdal/gdal_error_handler.cpp

    algorithms/spectral_indices.cpp
    algorithms/spectral_classification.cpp
    algorithms/band_math.cpp
    algorithms/atmospheric_correction.cpp
    algorithms/radiometric_calibration.cpp
    algorithms/change_detection.cpp
    algorithms/qa_mask.cpp
    algorithms/math_utils.cpp
    algorithms/mosaic.cpp
    algorithms/image_enhancement.cpp
    algorithms/chunked_processor.cpp
    algorithms/terrain_analysis.cpp
    algorithms/image_fusion.cpp
    algorithms/spectral_unmixing.cpp
    algorithms/spectral_anomaly.cpp
    algorithms/spectral_resampling.cpp
    algorithms/endmember_extraction.cpp
    algorithms/spectral_library.cpp
    algorithms/vector_optimizer.cpp
    algorithms/satellite_products.cpp

    framework/input_validator.cpp
    framework/algorithm_engine.cpp
    framework/algorithm_descriptor.cpp
    framework/algorithm_provider_adapter.h
    framework/qgs_processing_provider_adapter.cpp
    framework/qgs_processing_provider_adapter.h
    framework/atomic_algorithm_adapter.cpp
    framework/provider_algorithm_adapter.cpp
    framework/atomic_algorithm_registry.cpp
    framework/json_params_converter.h
    framework/tool_call_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_operator_context.cpp
    ${CMAKE_SOURCE_DIR}/src/operators/framework/rs_operator_error.cpp
    framework/python_algorithm_adapter.cpp
    framework/python_processing_provider_adapter.cpp
    framework/python_processing_provider_adapter.h
    framework/agent_tool_call_exporter.cpp
    # agent_workflow_executor.cpp lives in sicnu_task_center: it routes through TaskCenter/JobEngine.
    # task_center.cpp lives in sicnu_task_center (below): JobEngine link would
    # form a cycle if pulled into this shared lib (processing→jobs→operators→processing).
    framework/output_committer.cpp
    framework/collection_import_service.cpp

    providers/gdal_tools/provider.cpp
    providers/gdal_tools/gdal_tool_wrapper.cpp
    providers/gdal_tools/algorithms/gdal_translate.cpp
    providers/gdal_tools/algorithms/gdal_warp.cpp
    providers/gdal_tools/algorithms/gdal_info.cpp
    providers/gdal_tools/algorithms/gdal_dem.cpp
    providers/gdal_tools/algorithms/gdal_contour.cpp
    providers/gdal_tools/algorithms/gdal_polygonize.cpp
    providers/gdal_tools/algorithms/gdal_merge.cpp
    providers/gdal_tools/algorithms/gdal_calc.cpp
    providers/gdal_tools/algorithms/gdal_retile.cpp
    providers/gdal_tools/algorithms/gdal_proximity.cpp
    providers/gdal_tools/algorithms/gdal_sieve.cpp
    providers/gdal_tools/algorithms/gdal_fillnodata.cpp
    providers/gdal_tools/algorithms/gdal_grid.cpp
    providers/gdal_tools/algorithms/gdal_rasterize.cpp
    providers/gdal_tools/algorithms/gdalbuildvrt.cpp
    providers/gdal_tools/algorithms/gdaltindex.cpp
    providers/gdal_tools/algorithms/gdalmanage.cpp
    providers/gdal_tools/algorithms/gdaladdo.cpp
    providers/gdal_tools/algorithms/gdaltransform.cpp
    providers/gdal_tools/algorithms/gdal_edit.cpp
    providers/gdal_tools/algorithms/pct2rgb.cpp
    providers/gdal_tools/algorithms/rgb2pct.cpp
    providers/gdal_tools/algorithms/gdal2xyz.cpp
    providers/gdal_tools/algorithms/ogr2ogr.cpp
    providers/gdal_tools/algorithms/ogrinfo.cpp
    providers/gdal_tools/algorithms/ogrtindex.cpp

    providers/otb_tools/provider.cpp
    providers/otb_tools/otb_tool_wrapper.cpp
    providers/otb_tools/algorithms/otb_band_math.cpp
    providers/otb_tools/algorithms/otb_segmentation.cpp
    providers/otb_tools/algorithms/otb_extract_roi.cpp
    providers/otb_tools/algorithms/otb_concatenate_images.cpp
    providers/otb_tools/algorithms/otb_dynamic_convert.cpp
    providers/otb_tools/algorithms/otb_rescale.cpp
    providers/otb_tools/algorithms/otb_convert.cpp
    providers/otb_tools/algorithms/otb_mean_shift_smoothing.cpp
    providers/otb_tools/algorithms/otb_lsms.cpp
    providers/otb_tools/algorithms/otb_train_vector_classifier.cpp
    providers/otb_tools/algorithms/otb_image_classifier.cpp
    providers/otb_tools/algorithms/otb_kmeans_classification.cpp
    providers/otb_tools/algorithms/otb_feature_extraction.cpp
    providers/otb_tools/algorithms/otb_haralick_texture.cpp
    providers/otb_tools/algorithms/otb_radiometric_indices.cpp
    providers/otb_tools/algorithms/otb_ortho_rectification.cpp
    providers/otb_tools/algorithms/otb_bundle_to_perfect_sensor.cpp
    providers/otb_tools/algorithms/otb_superimpose.cpp
    providers/otb_tools/algorithms/otb_binary_morphological.cpp
    providers/otb_tools/algorithms/otb_band_math_x.cpp
    providers/otb_tools/algorithms/otb_compute_images_statistics.cpp
    providers/otb_tools/algorithms/otb_multi_resolution_pyramid.cpp
    providers/otb_tools/algorithms/otb_gray_scale_morphological.cpp
    providers/otb_tools/algorithms/otb_pixel_info.cpp
    providers/otb_tools/algorithms/otb_read_image_info.cpp
    providers/otb_tools/algorithms/otb_gray_level_cooccurrence_matrix.cpp
    providers/otb_tools/algorithms/otb_local_statistic_extraction.cpp
    providers/otb_tools/algorithms/otb_multivariate_alteration_detector.cpp
    providers/otb_tools/algorithms/otb_svm_classification.cpp
    providers/otb_tools/algorithms/otb_stereo_rectification.cpp

    providers/generic_cli/provider.cpp
    providers/generic_cli/generic_cli_algorithm.cpp

    providers/qgis_algorithms/provider.cpp
    providers/qgis_algorithms/algorithms/raster/raster_calculator.cpp
    providers/qgis_algorithms/algorithms/raster/raster_resample.cpp
    providers/qgis_algorithms/algorithms/raster/raster_clip.cpp
    providers/qgis_algorithms/algorithms/raster/raster_merge_bands.cpp
    providers/qgis_algorithms/algorithms/raster/raster_ndvi.cpp
    providers/qgis_algorithms/algorithms/raster/raster_statistics.cpp
    providers/qgis_algorithms/algorithms/vector/vector_buffer.cpp
    providers/qgis_algorithms/algorithms/vector/vector_clip.cpp
    providers/qgis_algorithms/algorithms/vector/vector_dissolve.cpp
    providers/qgis_algorithms/algorithms/vector/vector_merge.cpp
    providers/qgis_algorithms/algorithms/vector/vector_spatial_query.cpp
    providers/qgis_algorithms/algorithms/vector/vector_attribute_query.cpp
    providers/qgis_algorithms/algorithms/vector/vector_reproject.cpp
    providers/qgis_algorithms/algorithms/vector/vector_difference.cpp
    providers/qgis_algorithms/algorithms/vector/vector_symmetrical_difference.cpp
    providers/qgis_algorithms/algorithms/vector/vector_select_by_location.cpp
    providers/qgis_algorithms/algorithms/vector/vector_extract_by_location.cpp
    providers/qgis_algorithms/algorithms/vector/vector_field_calculator.cpp
    providers/qgis_algorithms/algorithms/vector/vector_nearest_neighbor.cpp
    providers/qgis_algorithms/algorithms/vector/vector_distance_matrix.cpp

    providers/qgis_algorithms/algorithms/remote_sensing/band_math_algorithm.cpp
    providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.cpp
    providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.cpp

    providers/qgis_algorithms/algorithms/native/native_centroids.cpp
    providers/qgis_algorithms/algorithms/native/native_convex_hull.cpp
    providers/qgis_algorithms/algorithms/native/native_simplify.cpp
    providers/qgis_algorithms/algorithms/native/native_clip.cpp
    providers/qgis_algorithms/algorithms/native/native_intersection.cpp
    providers/qgis_algorithms/algorithms/native/native_union.cpp
    providers/qgis_algorithms/algorithms/native/native_difference.cpp
    providers/qgis_algorithms/algorithms/native/native_extract_by_attribute.cpp
    providers/qgis_algorithms/algorithms/native/native_clip_raster.cpp
    providers/qgis_algorithms/algorithms/native/native_raster_statistics.cpp
    providers/qgis_algorithms/algorithms/native/native_hillshade.cpp
    providers/qgis_algorithms/algorithms/native/native_reproject_layer.cpp
    providers/qgis_algorithms/algorithms/native/native_assign_projection.cpp
)

target_include_directories(sicnu_processing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sicnu_processing PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Xml
    qgis_core
    GDAL::GDAL
)
# Sicnu::data is linked PRIVATE here: output_committer (in this target) is the
# only consumer of data headers from sicnu_processing. Downstream targets that
# need data headers (e.g. sicnu_operators' asset_index_pipeline) link
# Sicnu::data themselves.
target_link_libraries(sicnu_processing PRIVATE
    Sicnu::data
)
if(TARGET jsoncpp)
  target_link_libraries(sicnu_processing PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_processing PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_processing PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_processing PRIVATE jsoncpp)
endif()

# Task Center (#26–#29): separate static lib so JobEngine can be linked without
# a processing↔jobs↔operators cycle. OutputCommitter keeps only a TaskCenter*
# and resolves symbols when final targets also link sicnu_task_center.
add_library(sicnu_task_center STATIC
    framework/task_center.cpp
    framework/resource_monitor.cpp
    framework/agent_workflow_executor.cpp
    # Task Center seam for OutputCommitter and ToolCallDispatcher default constructor
    # — kept out of sicnu_processing so that library does not need JobEngine / TaskCenter link deps.
    framework/output_committer_task_center.cpp
    framework/tool_call_dispatcher_task_center.cpp
)
target_include_directories(sicnu_task_center PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sicnu_task_center PUBLIC
    Qt6::Core
    qgis_core
    sicnu_processing
    sicnu_jobs
    sicnu_workflow
)
set_target_properties(sicnu_task_center PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS sicnu_processing
    LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
)
