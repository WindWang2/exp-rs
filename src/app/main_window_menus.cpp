// main_window_menus.cpp — Menu bar, toolbars, and status bar setup
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

#include "dialogs/extract_band_dialog.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QLabel>
#include <QAction>
#include <QKeySequence>
#include <QList>
#include <QPair>

#include <qgsrasterlayer.h>


namespace {
void tip(QAction *a, const QString &t)
{
    if (!a) return;
    a->setToolTip(t);
    a->setStatusTip(t);
    a->setWhatsThis(t);
}
} // namespace

void QgisDesktopWindow::setupMenu()
{
    // Brand logo (left corner) — app icon + short product name
    QWidget *brandWidget = new QWidget(this);
    brandWidget->setObjectName("rsMenuBarBrand");
    QHBoxLayout *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(8, 0, 0, 0);
    brandLayout->setSpacing(6);
    QLabel *logo = new QLabel;
    logo->setObjectName("rsBrandLogo");
    {
        const QIcon ic(QStringLiteral(":/icons/app_icon"));
        if (!ic.isNull())
            logo->setPixmap(ic.pixmap(22, 22));
        else
            logo->setText(QStringLiteral("RS"));
    }
    QLabel *name = new QLabel("RS Studio");
    name->setObjectName("rsBrandName");
    brandLayout->addWidget(logo);
    brandLayout->addWidget(name);
    menuBar()->setCornerWidget(brandWidget, Qt::TopLeftCorner);

    // Version label (right corner)
    QLabel *versionLabel = new QLabel("v0.9.2-dev");
    versionLabel->setObjectName("rsBrandVersion");
    menuBar()->setCornerWidget(versionLabel, Qt::TopRightCorner);

    // Project Menu
    QMenu *projectMenu = menuBar()->addMenu(tr("&Project"));
    projectMenu->addAction(QIcon(":/icons/new_project"), tr("New Project"), this, &QgisDesktopWindow::newProject, QKeySequence::New);
    projectMenu->addAction(QIcon(":/icons/o_en"), tr("Open Project..."), this, &QgisDesktopWindow::openProject, QKeySequence::Open);
    projectMenu->addAction(QIcon(":/icons/s_ve"), tr("Save Project"), this, &QgisDesktopWindow::saveProject, QKeySequence::Save);
    projectMenu->addAction(tr("Save Project As..."), this, &QgisDesktopWindow::saveProjectAs);
    projectMenu->addSeparator();
    projectMenu->addAction(QIcon(":/icons/i_ort"), tr("Import Layer..."), this, &QgisDesktopWindow::importLayer);
    projectMenu->addAction(tr("Browse STAC Catalog..."), this, &QgisDesktopWindow::browseStacCatalog);
    projectMenu->addSeparator();
    projectMenu->addAction(tr("New Layout..."), this, &QgisDesktopWindow::newLayout);
    projectMenu->addSeparator();
    projectMenu->addAction(tr("Export Lab Report..."), this, &QgisDesktopWindow::exportLabReport);
    projectMenu->addSeparator();
    projectMenu->addAction(tr("Quit"), this, &QMainWindow::close, QKeySequence::Quit);

    // Edit Menu
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_toggleEditingAction = editMenu->addAction(QIcon(":/icons/mActionToggleEditing"), tr("Toggle Editing"), this, &QgisDesktopWindow::toggleEditing);
    m_toggleEditingAction->setCheckable(true);
    m_toggleEditingAction->setShortcut(QKeySequence("Ctrl+E"));
    m_saveEditsAction = editMenu->addAction(QIcon(":/icons/mActionSaveEdits"), tr("Save Edits"), this, &QgisDesktopWindow::saveEdits);
    m_saveEditsAction->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction(tr("Undo"), this, &QgisDesktopWindow::undo, QKeySequence::Undo);
    editMenu->addAction(tr("Redo"), this, &QgisDesktopWindow::redo, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("Cut Features"), this, &QgisDesktopWindow::cutFeatures, QKeySequence::Cut);
    editMenu->addAction(tr("Copy Features"), this, &QgisDesktopWindow::copyFeatures, QKeySequence::Copy);
    editMenu->addAction(tr("Paste Features"), this, &QgisDesktopWindow::pasteFeatures, QKeySequence::Paste);
    editMenu->addSeparator();
    editMenu->addAction(tr("Select All"), this, &QgisDesktopWindow::selectAll, QKeySequence("Ctrl+A"));
    editMenu->addAction(QIcon(":/icons/mActionSelectRectangle"), tr("Select Features"), this, &QgisDesktopWindow::selectFeatures);
    editMenu->addSeparator();
    editMenu->addAction(QIcon(":/icons/mActionDeleteSelectedFeatures"), tr("Delete Selected"), this, &QgisDesktopWindow::deleteSelectedFeatures, QKeySequence::Delete);
    editMenu->addSeparator();
    editMenu->addAction(tr("Open Attribute Table..."), this, &QgisDesktopWindow::openAttributeTable);

    // Digitize Menu
    QMenu *digitizeMenu = menuBar()->addMenu(tr("&Digitize"));
    digitizeMenu->addAction(QIcon(":/icons/mActionCapturePoint"), tr("Add Feature"), this, &QgisDesktopWindow::addFeature, QKeySequence("Ctrl+."));
    digitizeMenu->addAction(QIcon(":/icons/mActionVertexTool"), tr("Vertex Tool"), this, &QgisDesktopWindow::vertexTool, QKeySequence("Ctrl+V"));
    digitizeMenu->addSeparator();
    digitizeMenu->addAction(QIcon(":/icons/mActionMoveFeature"), tr("Move Feature"), this, &QgisDesktopWindow::moveFeature);
    digitizeMenu->addAction(QIcon(":/icons/mActionRotateFeature"), tr("Rotate Feature"), this, &QgisDesktopWindow::rotateFeature);
    digitizeMenu->addAction(QIcon(":/icons/mActionScaleFeature"), tr("Scale Feature"), this, &QgisDesktopWindow::scaleFeature);
    digitizeMenu->addAction(QIcon(":/icons/mActionOffsetCurve"), tr("Offset Curve"), this, &QgisDesktopWindow::offsetCurve);
    digitizeMenu->addAction(QIcon(":/icons/mActionReverseLine"), tr("Reverse Line"), this, &QgisDesktopWindow::reverseLine);
    digitizeMenu->addSeparator();
    digitizeMenu->addAction(QIcon(":/icons/mActionReshape"), tr("Reshape Geometry"), this, &QgisDesktopWindow::reshapeGeometry);
    digitizeMenu->addAction(QIcon(":/icons/mActionSplitFeatures"), tr("Split Features"), this, &QgisDesktopWindow::splitFeatures);
    digitizeMenu->addAction(tr("Split Parts"), this, &QgisDesktopWindow::splitParts);
    digitizeMenu->addAction(QIcon(":/icons/mActionSimplify"), tr("Simplify"), this, &QgisDesktopWindow::simplifyFeature);
    digitizeMenu->addSeparator();
    digitizeMenu->addAction(QIcon(":/icons/mActionAddRing"), tr("Add Ring"), this, &QgisDesktopWindow::addRing);
    digitizeMenu->addAction(QIcon(":/icons/mActionAddPart"), tr("Add Part"), this, &QgisDesktopWindow::addPart);
    digitizeMenu->addAction(QIcon(":/icons/mActionFillRing"), tr("Fill Ring"), this, &QgisDesktopWindow::fillRing);
    digitizeMenu->addAction(QIcon(":/icons/mActionDeletePart"), tr("Delete Part"), this, &QgisDesktopWindow::deletePart);
    digitizeMenu->addAction(QIcon(":/icons/mActionDeleteRing"), tr("Delete Ring"), this, &QgisDesktopWindow::deleteRing);
    digitizeMenu->addSeparator();
    digitizeMenu->addAction(QIcon(":/icons/mActionTrimExtendFeature"), tr("Trim/Extend Feature"), this, &QgisDesktopWindow::trimExtendFeature);
    digitizeMenu->addAction(QIcon(":/icons/mActionChamferFillet"), tr("Chamfer/Fillet"), this, &QgisDesktopWindow::chamferFillet);
    digitizeMenu->addAction(QIcon(":/icons/mActionFeatureArray"), tr("Feature Array"), this, &QgisDesktopWindow::featureArray);

    // View Menu
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(QIcon(":/icons/zoo_in"), tr("Zoom In"), this, &QgisDesktopWindow::zoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction(QIcon(":/icons/zoo_out"), tr("Zoom Out"), this, &QgisDesktopWindow::zoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction(QIcon(":/icons/full_extent"), tr("Zoom Full"), this, &QgisDesktopWindow::zoomFullExtent, QKeySequence("Ctrl+Shift+F"));
    viewMenu->addAction(tr("Zoom to Layer"), this, &QgisDesktopWindow::zoomToLayer, QKeySequence("Ctrl+L"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/p_n"), tr("Pan"), this, &QgisDesktopWindow::panMap, QKeySequence("Space"));
    viewMenu->addAction(QIcon(":/icons/identify"), tr("Identify"), this, &QgisDesktopWindow::identifyFeatures, QKeySequence("Ctrl+Shift+I"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/me_sure_dist"), tr("Measure Distance"), this, &QgisDesktopWindow::measureDistance, QKeySequence("Ctrl+Shift+D"));
    viewMenu->addAction(QIcon(":/icons/me_sure_are_"), tr("Measure Area"), this, &QgisDesktopWindow::measureArea, QKeySequence("Ctrl+Shift+A"));
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Compare Layers..."), this, &QgisDesktopWindow::openComparisonDialog, QKeySequence("Ctrl+Shift+C"));
    viewMenu->addAction(tr("Swipe Layers"), this, &QgisDesktopWindow::toggleSwipeTool, QKeySequence("Ctrl+Shift+S"));
    viewMenu->addSeparator();
    viewMenu->addAction(QIcon(":/icons/refresh_view"), tr("Refresh"), this, &QgisDesktopWindow::refreshMap, QKeySequence("F5"));

    // Layer Menu
    QMenu *layerMenu = menuBar()->addMenu(tr("&Layer"));
    layerMenu->addAction(QIcon(":/icons/r_ster"), tr("Add Raster Layer..."), this, &QgisDesktopWindow::addRasterLayer);
    layerMenu->addAction(QIcon(":/icons/vector"), tr("Add Vector Layer..."), this, &QgisDesktopWindow::addVectorLayer);
    layerMenu->addSeparator();
    layerMenu->addAction(tr("New Shapefile Layer..."), this, &QgisDesktopWindow::newVectorLayer);
    layerMenu->addSeparator();
    layerMenu->addAction(tr("Layer Properties..."), this, &QgisDesktopWindow::layerProperties, QKeySequence("Ctrl+I"));
    layerMenu->addAction(tr("Remove Layer"), this, &QgisDesktopWindow::removeLayer, QKeySequence("Ctrl+Shift+Delete"));
    layerMenu->addSeparator();
    layerMenu->addAction(tr("Set Project CRS..."), this, &QgisDesktopWindow::setProjectCrs);

    // Processing Menu
    QMenu *processingMenu = menuBar()->addMenu(tr("&Processing"));
    processingMenu->addAction(tr("Toolbox"), this, &QgisDesktopWindow::showProcessingToolbox);
    processingMenu->addSeparator();
    processingMenu->addAction(tr("History"), this, &QgisDesktopWindow::showProcessingHistory);
    processingMenu->addSeparator();
    processingMenu->addAction(tr("Batch Processing..."), this, &QgisDesktopWindow::openBatchProcessingDialog);

    // Raster Menu
    QMenu *rasterMenu = menuBar()->addMenu(tr("&Raster"));
    rasterMenu->addAction(QIcon(":/icons/r_ster_calc"), tr("Image Enhancement..."), this, &QgisDesktopWindow::openImageEnhancementPanel);
    rasterMenu->addAction(QIcon(":/icons/b_nd_m_th"), tr("Band Math..."), this, &QgisDesktopWindow::openBandMathDialog);
    rasterMenu->addAction(QIcon(":/icons/at_os_corr"), tr("Atmospheric Correction..."), this, &QgisDesktopWindow::openAtmosphericCorrectionDialog);
    rasterMenu->addAction(QIcon(":/icons/veget_tion_index"), tr("Vegetation Index..."), this, &QgisDesktopWindow::openSpectralIndexDialog);
    rasterMenu->addAction(QIcon(":/icons/mos_ic"), tr("Mosaic..."), this, &QgisDesktopWindow::openMosaicDialog);
    rasterMenu->addSeparator();
    QMenu *regMenu = rasterMenu->addMenu(tr("Image Registration"));
    regMenu->setObjectName(QStringLiteral("mImageRegistrationMenu"));
    regMenu->setToolTipsVisible(true);
    regMenu->setToolTip(tr("影像配准 / 几何校正：双影像或影像对地图。"));
    auto *i2iAct = regMenu->addAction(QIcon(QStringLiteral(":/icons/r_ster_calc")),
                       tr("Image 2 Image"),
                       this, &QgisDesktopWindow::openGeorefImageToImage);
    i2iAct->setToolTip(tr(
        "Image 2 Image：水平双画布 SRC|REF，同名点配准，支持 SIFT 自动匹配。"
        "不含 RPC。"));
    i2iAct->setStatusTip(i2iAct->toolTip());
    auto *i2mAct = regMenu->addAction(QIcon(QStringLiteral(":/icons/r_ster_calc")),
                       tr("Image 2 Map"),
                       this, &QgisDesktopWindow::openGeorefImageToMap);
    i2mAct->setToolTip(tr(
        "Image 2 Map：源影像 + 主工程地图预览取点，变换方法含 RPC Physical。"));
    i2mAct->setStatusTip(i2mAct->toolTip());
    rasterMenu->addAction(tr("Change Detection..."), this, &QgisDesktopWindow::openChangeDetectionDialog);
    // Phase 10A Task 10.2 — Classification submenu (Pixel-based + OBIA placeholder).
    auto *classifyMenu = rasterMenu->addMenu(tr("Classification"));
#ifdef SICNU_HAS_CLASSIFY
    classifyMenu->addAction(tr("Supervised Classification (Pixel-based)..."),
                            this, &QgisDesktopWindow::openClassificationWindow);
#ifdef SICNU_HAS_OBIA
    classifyMenu->addAction(tr("Object-based Classification (OBIA)..."),
                            this, &QgisDesktopWindow::openObiaWindow);
#else
    auto *obiaAct = classifyMenu->addAction(tr("Object-based Classification (OBIA) — Phase 10B"));
    obiaAct->setEnabled(false);
#endif
#else
    auto *disabledAct = classifyMenu->addAction(tr("Classification (OpenCV ml unavailable)"));
    disabledAct->setEnabled(false);
#endif
    rasterMenu->addSeparator();
    rasterMenu->addAction(QIcon(":/icons/extr_ct_b_nd"), tr("Extract Band..."), this, [this]() {
        ExtractBandDialog dlg(this);
        if (m_mapCanvas && m_mapCanvas->currentLayer()) {
            if (auto *rl = qobject_cast<QgsRasterLayer *>(m_mapCanvas->currentLayer()))
                dlg.setRasterLayer(rl);
        }
        dlg.exec();
    });
    rasterMenu->addAction(QIcon(":/icons/b_nd_co_bo"), tr("Band Composite..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:raster_merge_bands"); });
    rasterMenu->addSeparator();
    auto *enhanceMenu = rasterMenu->addMenu(tr("Enhancement"));
    enhanceMenu->addAction(tr("Contrast Stretch..."), this, &QgisDesktopWindow::openContrastStretchDialog);
    enhanceMenu->addAction(tr("Spatial Filter..."), this, &QgisDesktopWindow::openSpatialFilterDialog);
    enhanceMenu->addAction(tr("PCA..."), this, &QgisDesktopWindow::openPcaDialog);
    enhanceMenu->addAction(tr("Band Ratio / IHS..."), this, &QgisDesktopWindow::openBandRatioDialog);
    enhanceMenu->addSeparator();
    enhanceMenu->addAction(tr("Speckle Filter (SAR)..."), this, &QgisDesktopWindow::openSpeckleFilterDialog);

    auto *terrainMenu = rasterMenu->addMenu(tr("Terrain Analysis"));
    terrainMenu->addAction(tr("Slope / Aspect / Hillshade..."), this, &QgisDesktopWindow::openTerrainDialog);

    rasterMenu->addAction(tr("Image Fusion..."), this, &QgisDesktopWindow::openFusionDialog);

    // Vector Menu
    QMenu *vectorMenu = menuBar()->addMenu(tr("&Vector"));
    vectorMenu->addAction(QIcon(":/icons/buffer"), tr("Buffer..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_buffer"); });
    vectorMenu->addAction(QIcon(":/icons/dissolve"), tr("Dissolve..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_dissolve"); });
    vectorMenu->addAction(QIcon(":/icons/merge"), tr("Merge..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_merge"); });
    vectorMenu->addAction(QIcon(":/icons/cli_"), tr("Clip..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_clip"); });
    vectorMenu->addSeparator();
    vectorMenu->addAction(tr("Difference..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:native_difference"); });
    vectorMenu->addAction(tr("Intersection..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:native_intersection"); });
    vectorMenu->addAction(tr("Union..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:native_union"); });
    vectorMenu->addSeparator();
    vectorMenu->addAction(tr("Select by Location..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_select_by_location"); });
    vectorMenu->addAction(tr("Extract by Location..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_extract_by_location"); });
    vectorMenu->addAction(tr("Reproject..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_reproject"); });
    vectorMenu->addAction(tr("Field Calculator..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_field_calculator"); });
    vectorMenu->addAction(tr("Nearest Neighbor..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_nearest_neighbor"); });
    vectorMenu->addAction(tr("Distance Matrix..."), this, [this](){ openProcessingAlgorithm("qgis_algorithms:vector_distance_matrix"); });

    // Settings Menu
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    settingsMenu->addAction(QIcon(":/icons/settings"), tr("Options..."), this, &QgisDesktopWindow::options);
    settingsMenu->addSeparator();
    settingsMenu->addAction(QIcon(":/icons/define_crs"), tr("CRS Presets..."), this, &QgisDesktopWindow::openCrsPresetDialog);

    // Window Menu (dock toggle actions added in setupDockWidgets)
    m_windowMenu = menuBar()->addMenu(tr("&Window"));

    // Help Menu
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(QIcon(":/icons/hel_"), tr("Help Contents"), this, &QgisDesktopWindow::helpContents, QKeySequence::HelpContents);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Load Sample Data"), this, &QgisDesktopWindow::loadSampleData);
    helpMenu->addAction(tr("Guided Workflows"), this, &QgisDesktopWindow::showGuidedWorkflows);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Check Version"), this, &QgisDesktopWindow::checkVersion);
    helpMenu->addAction(tr("About"), this, &QgisDesktopWindow::about);
}

void QgisDesktopWindow::setupToolbars()
{
    // File Toolbar
    QToolBar *fileToolBar = addToolBar("File");
    fileToolBar->setObjectName("fileToolBar");
    fileToolBar->setIconSize(QSize(24, 24));
    fileToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    fileToolBar->addAction(QIcon(":/icons/new_project"), tr("New Project"), this, &QgisDesktopWindow::newProject)->setToolTip(tr("New Project (Ctrl+N)"));
    fileToolBar->addAction(QIcon(":/icons/o_en"), tr("Open Project"), this, &QgisDesktopWindow::openProject)->setToolTip(tr("Open Project (Ctrl+O)"));
    fileToolBar->addAction(QIcon(":/icons/s_ve"), tr("Save Project"), this, &QgisDesktopWindow::saveProject)->setToolTip(tr("Save Project (Ctrl+S)"));
    fileToolBar->addSeparator();
    fileToolBar->addAction(QIcon(":/icons/r_ster"), tr("Add Raster Layer"), this, &QgisDesktopWindow::addRasterLayer)->setToolTip(tr("Add Raster Layer"));
    fileToolBar->addAction(QIcon(":/icons/vector"), tr("Add Vector Layer"), this, &QgisDesktopWindow::addVectorLayer)->setToolTip(tr("Add Vector Layer"));

    // Map Tools Toolbar
    QToolBar *mapToolsToolBar = addToolBar("Map Tools");
    mapToolsToolBar->setObjectName("mapToolsToolBar");
    mapToolsToolBar->setIconSize(QSize(24, 24));
    mapToolsToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    mapToolsToolBar->addAction(QIcon(":/icons/p_n"), tr("Pan"), this, &QgisDesktopWindow::panMap)->setToolTip(tr("Pan (Space)"));
    mapToolsToolBar->addAction(QIcon(":/icons/zoo_in"), tr("Zoom In"), this, &QgisDesktopWindow::zoomIn)->setToolTip(tr("Zoom In (Ctrl+Plus)"));
    mapToolsToolBar->addAction(QIcon(":/icons/zoo_out"), tr("Zoom Out"), this, &QgisDesktopWindow::zoomOut)->setToolTip(tr("Zoom Out (Ctrl+Minus)"));
    mapToolsToolBar->addAction(QIcon(":/icons/full_extent"), tr("Full Extent"), this, &QgisDesktopWindow::zoomFullExtent)->setToolTip(tr("Zoom Full Extent (Ctrl+Shift+F)"));
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addAction(QIcon(":/icons/identify"), tr("Identify"), this, &QgisDesktopWindow::identifyFeatures)->setToolTip(tr("Identify Features (Ctrl+Shift+I)"));
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addAction(QIcon(":/icons/me_sure_dist"), tr("Measure Distance"), this, &QgisDesktopWindow::measureDistance)->setToolTip(tr("Measure Distance (Ctrl+Shift+D)"));
    mapToolsToolBar->addAction(QIcon(":/icons/me_sure_are_"), tr("Measure Area"), this, &QgisDesktopWindow::measureArea)->setToolTip(tr("Measure Area (Ctrl+Shift+A)"));

    // CRS Selector in toolbar
    m_crsSelector = new QgsProjectionSelectionWidget(mapToolsToolBar);
    m_crsSelector->setOptionVisible(QgsProjectionSelectionWidget::ProjectCrs, true);
    connect(m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
            this, &QgisDesktopWindow::onCrsChanged);
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addWidget(m_crsSelector);

    // Remote Sensing Toolbar
    QToolBar *rsToolBar = addToolBar("Remote Sensing");
    rsToolBar->setObjectName("rsToolBar");
    rsToolBar->setIconSize(QSize(24, 24));
    rsToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    rsToolBar->addAction(QIcon(":/icons/veget_tion_index"), tr("Vegetation Index"), this, &QgisDesktopWindow::openSpectralIndexDialog)->setToolTip(tr("Vegetation Index (NDVI, EVI, etc.)"));
    rsToolBar->addAction(QIcon(":/icons/b_nd_co_bo"), tr("Band Composition"), this, [this](){ openProcessingAlgorithm("qgis_algorithms:raster_merge_bands"); })->setToolTip(tr("Band Composition"));
    rsToolBar->addAction(QIcon(":/icons/at_os_corr"), tr("Atmospheric Correction"), this, &QgisDesktopWindow::openAtmosphericCorrectionDialog)->setToolTip(tr("Atmospheric Correction (DOS1/DOS2)"));
    rsToolBar->addAction(QIcon(":/icons/mos_ic"), tr("Mosaic"), this, &QgisDesktopWindow::openMosaicDialog)->setToolTip(tr("Mosaic / Stitching"));
    rsToolBar->addSeparator();

    // Digitizing Toolbar
    QToolBar *digitizeToolBar = addToolBar("Digitizing");
    digitizeToolBar->setObjectName("digitizeToolBar");
    digitizeToolBar->setIconSize(QSize(24, 24));
    digitizeToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_toggleEditingAction->setToolTip(tr("Toggle Editing (Ctrl+E)"));
    digitizeToolBar->addAction(m_toggleEditingAction);
    m_saveEditsAction->setToolTip(tr("Save Edits"));
    digitizeToolBar->addAction(m_saveEditsAction);
    digitizeToolBar->addSeparator();
    auto *actSelect = digitizeToolBar->addAction(QIcon(":/icons/mActionSelectRectangle"), tr("Select"), this, &QgisDesktopWindow::selectFeatures);
    actSelect->setToolTip(tr("Select Features"));
    auto *actAddFeature = digitizeToolBar->addAction(QIcon(":/icons/mActionCapturePoint"), tr("Add Feature"), this, &QgisDesktopWindow::addFeature);
    actAddFeature->setToolTip(tr("Add Feature"));
    auto *actVertex = digitizeToolBar->addAction(QIcon(":/icons/mActionVertexTool"), tr("Vertex"), this, &QgisDesktopWindow::vertexTool);
    actVertex->setToolTip(tr("Vertex Tool"));
    digitizeToolBar->addSeparator();
    auto *actMove = digitizeToolBar->addAction(QIcon(":/icons/mActionMoveFeature"), tr("Move"), this, &QgisDesktopWindow::moveFeature);
    actMove->setToolTip(tr("Move Feature"));
    auto *actRotate = digitizeToolBar->addAction(QIcon(":/icons/mActionRotateFeature"), tr("Rotate"), this, &QgisDesktopWindow::rotateFeature);
    actRotate->setToolTip(tr("Rotate Feature"));
    auto *actReshape = digitizeToolBar->addAction(QIcon(":/icons/mActionReshape"), tr("Reshape"), this, &QgisDesktopWindow::reshapeGeometry);
    actReshape->setToolTip(tr("Reshape Geometry"));
    auto *actSplit = digitizeToolBar->addAction(QIcon(":/icons/mActionSplitFeatures"), tr("Split"), this, &QgisDesktopWindow::splitFeatures);
    actSplit->setToolTip(tr("Split Features"));
    digitizeToolBar->addSeparator();
    auto *actOffset = digitizeToolBar->addAction(QIcon(":/icons/mActionOffsetCurve"), tr("Offset"), this, &QgisDesktopWindow::offsetCurve);
    actOffset->setToolTip(tr("Offset Curve"));
    auto *actSimplify = digitizeToolBar->addAction(QIcon(":/icons/mActionSimplify"), tr("Simplify"), this, &QgisDesktopWindow::simplifyFeature);
    actSimplify->setToolTip(tr("Simplify"));
    auto *actReverse = digitizeToolBar->addAction(QIcon(":/icons/mActionReverseLine"), tr("Reverse"), this, &QgisDesktopWindow::reverseLine);
    actReverse->setToolTip(tr("Reverse Line"));
    auto *actAddRing = digitizeToolBar->addAction(QIcon(":/icons/mActionAddRing"), tr("Add Ring"), this, &QgisDesktopWindow::addRing);
    actAddRing->setToolTip(tr("Add Ring"));
    auto *actFillRing = digitizeToolBar->addAction(QIcon(":/icons/mActionFillRing"), tr("Fill Ring"), this, &QgisDesktopWindow::fillRing);
    actFillRing->setToolTip(tr("Fill Ring"));
    auto *actDelPart = digitizeToolBar->addAction(QIcon(":/icons/mActionDeletePart"), tr("Delete Part"), this, &QgisDesktopWindow::deletePart);
    actDelPart->setToolTip(tr("Delete Part"));

    // Editing tool actions — enabled only when a vector layer is in editing mode
    m_editingToolActions = { actSelect, actAddFeature, actVertex, actMove, actRotate,
                             actReshape, actSplit, actOffset, actSimplify, actReverse,
                             actAddRing, actFillRing, actDelPart };
    for (QAction *a : m_editingToolActions)
        a->setEnabled(false);
    rsToolBar->addAction(QIcon(":/icons/r_ster_calc"), tr("Raster Calculator"), this, [this](){ openProcessingAlgorithm("qgis_algorithms:raster_calculator"); })->setToolTip(tr("Raster Calculator"));
    rsToolBar->addAction(QIcon(":/icons/su_ervised"), tr("Supervised Classification"), this, &QgisDesktopWindow::openClassificationWindow)->setToolTip(tr("Supervised Classification"));
    rsToolBar->addAction(QIcon(":/icons/b_nd_m_th"), tr("Band Math"), this, &QgisDesktopWindow::openBandMathDialog)->setToolTip(tr("Band Math Expression"));

    // Detailed tooltips for common actions
    auto setTipByText = [this](const QString &label, const QString &tipText) {
        for (QAction *a : findChildren<QAction *>()) {
            if (!a) continue;
            if (a->text().remove(QLatin1Char('&')) == label) {
                a->setToolTip(tipText);
                a->setStatusTip(tipText);
                a->setWhatsThis(tipText);
            }
        }
    };
    const QList<QPair<QString, QString>> tips = {
        { tr("New Project"), tr("创建空白工程，清除当前图层与视图状态。") },
        { tr("Open Project..."), tr("打开已保存的工程文件。") },
        { tr("Save Project"), tr("保存当前工程到已有路径。") },
        { tr("Save Project As..."), tr("将工程另存为新文件。") },
        { tr("Import Layer..."), tr("导入栅格或矢量图层到工程。") },
        { tr("Browse STAC Catalog..."), tr("浏览 STAC 目录检索遥感数据。") },
        { tr("Quit"), tr("退出应用程序。") },
        { tr("Zoom In"), tr("放大地图视图。") },
        { tr("Zoom Out"), tr("缩小地图视图。") },
        { tr("Pan Map"), tr("平移地图。") },
        { tr("Full Extent"), tr("缩放到所有图层范围。") },
        { tr("Identify Features"), tr("点击地图查询要素/像元属性。") },
        { tr("Measure Distance"), tr("量测距离。") },
        { tr("Measure Area"), tr("量测面积。") },
        { tr("Toggle Editing"), tr("开启/关闭当前矢量图层编辑。") },
        { tr("Save Edits"), tr("保存矢量编辑。") },
        { tr("Select Features"), tr("矩形选择要素。") },
        { tr("Delete Selected"), tr("删除选中要素。") },
        { tr("Open Attribute Table..."), tr("打开属性表。") },
        { tr("Add Feature"), tr("数字化添加新要素。") },
        { tr("Vertex Tool"), tr("编辑节点。") },
        { tr("Processing Toolbox"), tr("打开处理工具箱，运行 GDAL/OTB/内置算法。") },
        { tr("Image Enhancement..."), tr("影像增强与显示拉伸。") },
        { tr("Band Math..."), tr("波段运算表达式计算。") },
        { tr("Atmospheric Correction..."), tr("大气校正对话框。") },
        { tr("Vegetation Index..."), tr("植被/光谱指数计算。") },
        { tr("Mosaic..."), tr("多景影像镶嵌。") },
        { tr("Change Detection..."), tr("变化检测分析。") },
        { tr("Supervised Classification (Pixel-based)..."), tr("打开监督分类窗口（像元级）。") },
        { tr("Object-based Classification (OBIA)..."), tr("面向对象分类 (OBIA)。") },
        { tr("Image 2 Image"), tr("双影像配准：SRC|REF，支持 SIFT。") },
        { tr("Image 2 Map"), tr("影像对主地图配准，支持 RPC。") },
        { tr("Help Contents"), tr("打开帮助文档。") },
    };
    for (const auto &p : tips)
        setTipByText(p.first, p.second);

    // Any remaining actions without tip: use cleaned label
    for (QAction *a : findChildren<QAction *>()) {
        if (!a || a->isSeparator()) continue;
        if (!a->toolTip().isEmpty() && a->toolTip() != a->text()) continue;
        const QString t = a->text().remove(QLatin1Char('&')).trimmed();
        if (t.isEmpty()) continue;
        if (a->toolTip().isEmpty() || a->toolTip() == a->text() || a->toolTip() == t)
        {
            // keep existing detailed tips; only fill empty
        }
        if (a->toolTip().isEmpty()) {
            a->setToolTip(t);
            a->setStatusTip(t);
        }
    }

}
void QgisDesktopWindow::setupStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName("rsStatusBar");
    bar->setFixedHeight(22);

    // Ready status (left side)
    m_readyLabel = new QLabel("Ready", bar);
    m_readyLabel->setObjectName("rsReadyLabel");
    bar->addWidget(m_readyLabel);

    // Coordinates display
    m_coordinatesLabel = new QLabel("0.000000, 0.000000", bar);
    m_coordinatesLabel->setObjectName("rsCoordLabel");
    bar->addPermanentWidget(m_coordinatesLabel);

    // Scale display
    m_scaleLabel = new QLabel("Scale: 1:1,000", bar);
    m_scaleLabel->setObjectName("rsScaleLabel");
    bar->addPermanentWidget(m_scaleLabel);

    // CRS display
    m_crsLabel = new QLabel("EPSG:3857", bar);
    m_crsLabel->setObjectName("rsCrsLabel");
    bar->addPermanentWidget(m_crsLabel);

    // Render time display
    m_renderTimeLabel = new QLabel("", bar);
    m_renderTimeLabel->setObjectName("rsRenderLabel");
    bar->addPermanentWidget(m_renderTimeLabel);

    // Cache usage
    m_cacheLabel = new QLabel("Cache: 0 MB", bar);
    m_cacheLabel->setObjectName("rsCacheLabel");
    bar->addPermanentWidget(m_cacheLabel);
}
