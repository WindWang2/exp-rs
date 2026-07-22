target_sources(qgis_analysis PRIVATE
    rs_segment_map.cpp
    rs_segment_features.cpp
    rs_simple_segmenter.cpp
    # Hierarchical OBIA V1
    rs_parent_link.cpp
    rs_object_hierarchy.cpp
    rs_otb_segmenter.cpp
    rs_hierarchy_features.cpp
    rs_class_raster.cpp
    rs_object_classify.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ToolPathManager (OTB discovery) lives under src/processing.
target_include_directories(qgis_analysis PRIVATE ${CMAKE_SOURCE_DIR}/src/processing)
target_link_libraries(qgis_analysis PRIVATE sicnu_processing)
# QColor is part of the public RsClassRaster / RsClassDef paint API (palette writeback).
# Gui is already required transitively by classification headers that expose QColor.
target_link_libraries(qgis_analysis PUBLIC Qt6::Gui)
