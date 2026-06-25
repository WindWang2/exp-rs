target_sources(qgis_analysis PRIVATE
    rs_segment_map.cpp
    rs_segment_features.cpp
    rs_simple_segmenter.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qgis_analysis PRIVATE sicnu_processing)
