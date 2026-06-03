target_sources(qgis_analysis PRIVATE
    rs_class_def.cpp
    rs_roi.cpp
    rs_roi_collection.cpp
    rs_roi_io.cpp
    rs_pixel_rasterizer.cpp
)
target_include_directories(qgis_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Phase 10A Task 10.6 — Jeffries-Matusita separability uses OpenCV core
# (cv::Mat, cv::calcCovarMatrix, cv::determinant). When OpenCV is absent
# the algorithm is unavailable along with the rest of the classification
# stack (the qgis_app_classify GUI library is also skipped — see
# src/app/classification/CMakeLists.txt).
if(SICNU_HAS_OPENCV)
    # Phase 10A Task 10.7 — RsFloodFill is OpenCV-dependent (cv::Mat).
    # Phase 10A Task 10.8 — Classifier backends use cv::ml.
    target_sources(qgis_analysis PRIVATE
        rs_jm_separability.cpp
        rs_flood_fill.cpp
        rs_classifier_backend.cpp
        rs_classifier_normalbayes.cpp
        rs_classifier_svm.cpp
        rs_classifier_kmeans.cpp
        # Phase 10A review patch — stratified train/test split helper.
        rs_classification_split.cpp
    )
    target_link_libraries(qgis_analysis PUBLIC ${OpenCV_LIBS})
    target_include_directories(qgis_analysis PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OPENCV=1)
endif()
