target_sources(qgis_analysis PRIVATE
    rs_class_def.cpp
    rs_roi.cpp
    rs_roi_collection.cpp
    rs_roi_io.cpp
    rs_pixel_rasterizer.cpp
    # Classification v1.1 — viewport → pixel window (no OpenCV).
    rs_pixel_window.cpp
    rs_pixel_ignore_options.cpp
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
        # Phase 10A Task 10.9 — confusion matrix + Kappa + per-class P/R/F1.
        rs_accuracy_assessment.cpp
        # Phase 10A.1.1 — Munkres O(n^3) Hungarian min-cost assignment.
        rs_hungarian_assignment.cpp
        # Phase 10A.1.2 — Stratified 5-fold cross validation.
        rs_cross_validation.cpp
        # Async cross-validation task (QgsTask-based, non-blocking UI).
        rs_cv_task.cpp
        rs_cv_task.h
        rs_feature_scaler.cpp
        # Classification workflow — post-process pure operators (sieve/majority/clump/recode).
        rs_post_process.cpp
        # Classification workflow — project (.rscproj) JSON persistence.
        rs_classification_project.cpp
    )
    target_link_libraries(qgis_analysis PUBLIC ${OpenCV_LIBS})
    target_include_directories(qgis_analysis PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OPENCV=1)
endif()
