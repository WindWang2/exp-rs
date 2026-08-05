cmake_minimum_required(VERSION 3.20)

# Phase 10A — Pixel-Based Classification GUI shell.
#
# Strong dependency on OpenCV (ml component required for Phase 10A.8 RF/SVM).
# Without OpenCV the static library is skipped entirely; the main app falls
# back to a disabled menu entry guarded by SICNU_HAS_CLASSIFY.
if(NOT SICNU_HAS_OPENCV)
    message(STATUS "Skipping qgis_app_classify — OpenCV ml component unavailable")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_library(qgis_app_classify STATIC
    qgsclassificationmainwindow.cpp
    rs_class_table_widget.cpp
    rs_class_quick_list.cpp
    rs_roi_tool_base.cpp
    rs_roi_tool_point.cpp
    rs_roi_tool_rectangle.cpp
    rs_roi_tool_polygon.cpp
    rs_roi_tool_freehand.cpp
    rs_roi_tool_magicwand.cpp
    rs_spectral_curve_widget.cpp
    rs_jm_matrix_widget.cpp
    # Phase 10A Task 10.8 — ClassifierBar (the QgsTask adapter was deleted
    # in ADR 0053; the window builds RsClassificationPipeline::Config
    # directly at the job seam).
    rs_classifier_setup_bar.cpp
    # Phase 10A Task 10.9 — accuracy assessment result dialog + embed panel.
    rs_accuracy_dialog.cpp
    rs_accuracy_panel.cpp
    # Phase 10A.1.3 — load classifier model from disk dialog.
    rs_classifier_load_dialog.cpp
    # Classification v1.1 — dirty close + workflow QSettings.
    rs_classify_session_state.cpp
    # Classification workflow — step state / soft gates / mode.
    rs_classify_workflow_controller.cpp
    # Runtime session bridge (lab.classify.supervised).
    rs_classify_workflow_bridge.cpp
    # Classification workflow — stepper bar + step host skeleton.
    rs_classify_stepper_bar.cpp
    ../dialogs/dialog_help_catalog.cpp
    ../dialogs/dialog_utils.cpp
    rs_classify_step_host.cpp
    # Classification workflow — post-process task + dialog.
    rs_post_process_task.cpp
    rs_post_process_dialog.cpp
    # Classification workflow — merge sub-classes target chooser.
    rs_merge_classes_dialog.cpp
    # ADR 0019 — GuiJobHandle lives on sicnu_geo_rs (shell QObject TU, see
    # tests/CMakeLists.txt MOC ODR note); tests compile it directly.
)

# Layer tree for session layer manager
target_include_directories(qgis_app_classify PUBLIC
    ${CMAKE_SOURCE_DIR}/src/core/layertree
    ${CMAKE_SOURCE_DIR}/src/gui/layertree
)

target_include_directories(qgis_app_classify PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/geometry
    ${CMAKE_SOURCE_DIR}/src/core/proj
    ${CMAKE_SOURCE_DIR}/src/core/providers/memory
    ${CMAKE_SOURCE_DIR}/src/core/symbology
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/gui/maptools
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(qgis_app_classify PUBLIC
    qgis_analysis
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_workflow
    sicnu_jobs
    sicnu_task_center
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
)

target_compile_definitions(qgis_app_classify PUBLIC SICNU_HAS_OPENCV=1)

set_target_properties(qgis_app_classify PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

target_compile_features(qgis_app_classify PUBLIC cxx_std_17)
