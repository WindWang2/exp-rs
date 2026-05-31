add_library(sicnu_processing SHARED
    tools/tool_path_manager.cpp

    gdal/gdal_dataset_wrapper.cpp
    gdal/gdal_error_handler.cpp

    algorithms/spectral_indices.cpp
    algorithms/band_math.cpp
    algorithms/atmospheric_correction.cpp

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
)

target_include_directories(sicnu_processing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sicnu_processing PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    GDAL::GDAL
)

install(TARGETS sicnu_processing
    LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
)
