target_sources(qgis_analysis PRIVATE
    rs_segment_map.cpp
    rs_segment_features.cpp
    rs_simple_segmenter.cpp
    rs_multires_segmenter.cpp
    # Hierarchical OBIA V1
    rs_parent_link.cpp
    rs_object_hierarchy.cpp
    rs_otb_segmenter.cpp
    rs_hierarchy_features.cpp
    rs_class_raster.cpp
    rs_object_classify.cpp
    rs_hierarchy_class_consolidator.cpp
    # ADR 0060 — canonical ROI-majority segment labeling (rs_majority_vote.h
    # is header-only and needs no source entry).
    rs_roi_labeler.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ToolPathManager (OTB discovery) lives under src/processing.
target_include_directories(qgis_analysis PRIVATE ${CMAKE_SOURCE_DIR}/src/processing)
target_link_libraries(qgis_analysis PRIVATE sicnu_processing)
# QColor is part of the public RsClassRaster / RsClassDef paint API (palette writeback).
# Gui is already required transitively by classification headers that expose QColor.
target_link_libraries(qgis_analysis PUBLIC Qt6::Gui)
