target_sources(qgis_analysis PRIVATE
    rs_class_def.cpp
    rs_roi.cpp
    rs_roi_collection.cpp
    rs_roi_io.cpp
    rs_pixel_rasterizer.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
