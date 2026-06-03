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
    target_sources(qgis_analysis PRIVATE rs_jm_separability.cpp)
    target_link_libraries(qgis_analysis PUBLIC ${OpenCV_LIBS})
    target_include_directories(qgis_analysis PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OPENCV=1)
endif()
