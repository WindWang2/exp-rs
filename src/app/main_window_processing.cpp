// main_window_processing.cpp — RS processing dialog slots
// Extracted from main_window.cpp for maintainability
//
// Migration note (W1–W5): high-frequency RS tools prefer WorkflowRuntime via
// openWorkflowTool(definitionId) when m_sessionController is available.
// Legacy QDialog paths remain as fallback (no session controller / headless
// callers) and for tools not yet registered as atomic definitions.
#include "main_window.h"

#include "project_context.h"

#include "dialogs/image_enhancement_panel.h"
#include "dialogs/band_math_dialog.h"
#include "dialogs/spectral_index_dialog.h"
#include "dialogs/atmospheric_dialog.h"
#include "dialogs/qa_mask_dialog.h"
#include "dialogs/apply_mask_dialog.h"
#include "dialogs/spectral_library_dialog.h"
#include "dialogs/post_classification_dialog.h"
#include "widgets/spectral_profile_widget.h"
#include "map_tools/rs_roi_spectrum_tool.h"
#include "dialogs/radiometric_calibration_dialog.h"
#include "dialogs/orthorectification_dialog.h"
#include "dialogs/contrast_stretch_dialog.h"
#include "dialogs/spatial_filter_dialog.h"
#include "dialogs/speckle_filter_dialog.h"
#include "dialogs/pca_dialog.h"
#include "dialogs/band_ratio_dialog.h"
#include "dialogs/terrain_dialog.h"
#include "dialogs/fusion_dialog.h"
#include "dialogs/mosaic_dialog.h"
#include "dialogs/temporal_analysis_dialog.h"
#include "dialogs/change_detection_dialog.h"
#include "dialogs/crs_preset_dialog.h"
#include "dialogs/comparison_dialog.h"
#include "dialogs/batch_processing_dialog.h"
#include "dialogs/sicnu_algorithm_dialog.h"
#include "map_tools/swipe_map_tool.h"
#include "widgets/histogram_stretch_widget.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsdockwidget.h>
#include <QMessageBox>
#include <QStatusBar>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsmapcanvas.h>
#include <processing/qgsprocessingregistry.h>

#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QStatusBar>

// ---------------------------------------------------------------------------
// Helper: find active/selected raster layer (no project-wide fallback)
// ---------------------------------------------------------------------------

static QgsRasterLayer *findActiveRaster(QgisDesktopWindow *win)
{
    // Try active layer first
    QgsRasterLayer *rl = qobject_cast<QgsRasterLayer*>(win->activeLayer());
    if (rl) return rl;

    // Try selected layers
    for (QgsMapLayer *layer : win->selectedLayers()) {
        if (layer->type() == Qgis::LayerType::Raster)
            return qobject_cast<QgsRasterLayer*>(layer);
    }

    return nullptr;
}

// Helper: find raster layer with project-wide fallback (for legacy dialogs)
// ---------------------------------------------------------------------------

static QgsRasterLayer *findAnyRaster(QgisDesktopWindow *win)
{
    QgsRasterLayer *rl = findActiveRaster(win);
    if (rl) return rl;

    // Fallback: first raster layer in project
    for (QgsMapLayer *layer : QgsProject::instance()->mapLayers().values()) {
        if (layer->type() == Qgis::LayerType::Raster)
            return qobject_cast<QgsRasterLayer*>(layer);
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: open a raster processing dialog and load result
// ---------------------------------------------------------------------------

template<typename DialogType>
static void openRasterDialog(QgisDesktopWindow *win, const QString &title,
                             QgsRasterLayer *rasterLayer = nullptr)
{
    DialogType dialog(win);
    if (rasterLayer) {
        dialog.setRasterLayer(rasterLayer);
    }
    // Review dialogs (shouldAutoAcceptOnSuccess()==false) stay open after
    // completion and own the single result load themselves (#674 review):
    // the TaskCenter auto-load is disabled for this seam.
    QObject::connect(&dialog, &RasterProcessingDialogBase::resultReadyForDisplay,
                     win, &QgisDesktopWindow::loadRasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            win->loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Processing algorithm dialog (from Processing Toolbox)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openProcessingAlgorithm(const QString &algorithmId)
{
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (!alg)
    {
        QMessageBox::warning(this, tr("Algorithm Not Found"),
                             tr("Could not find processing algorithm: %1").arg(algorithmId));
        return;
    }

    std::unique_ptr<QgsProcessingAlgorithm> algorithm(alg->create());
    if (!algorithm)
        return;

    auto *dlg = new SicnuAlgorithmDialog(this);
    dlg->setAlgorithm(algorithm.release());
    dlg->buildParameterWidgets();
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ---------------------------------------------------------------------------
// Image Enhancement Panel
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openImageEnhancementPanel()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Image Enhancement"),
                                 tr("Please select a raster layer first."));
        return;
    }
    ImageEnhancementPanel dialog(this);
    dialog.setRasterLayer(rasterLayer);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Band Math dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBandMathDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.band_math" ) );
        return;
    }
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Math"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<BandMathDialog>(this, tr("Band Math"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Spectral Index dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpectralIndexDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.spectral_index" ) );
        return;
    }
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spectral Index"),
                                 tr("Please select a raster layer first."));
        return;
    }

    SpectralIndexDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    // Let the user pick a registered Data Asset as input and commit the output
    // transactionally, when a Data Manager is available.
    if (m_projectContext)
        dialog.setDataManager(&m_projectContext->dataManager());
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Atmospheric Correction dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openAtmosphericCorrectionDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.atmospheric_correction" ) );
        return;
    }
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Atmospheric Correction"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<AtmosphericDialog>(this, tr("Atmospheric Correction"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Radiometric calibration dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openRadiometricCalibrationDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("辐射定标"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    openRasterDialog<RadiometricCalibrationDialog>(this, tr("辐射定标"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Orthorectification dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openOrthorectificationDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("正射纠正"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    openRasterDialog<OrthorectificationDialog>(this, tr("正射纠正"), rasterLayer);
}

// ---------------------------------------------------------------------------
// QA Mask dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openQaMaskDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("QA 掩膜"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    openRasterDialog<QaMaskDialog>(this, tr("QA 掩膜"), rasterLayer);
}

void QgisDesktopWindow::openApplyMaskDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("应用掩膜"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    ApplyMaskDialog dlg(this);
    dlg.setRasterLayer(rasterLayer);
    dlg.exec();
}

void QgisDesktopWindow::openPostClassificationDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer)
    {
        QMessageBox::information(this, tr("后分类比较"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    PostClassificationDialog dlg(this);
    dlg.populateLayers();
    // Never loads on accept (#674 review): the dialog's own signal is the
    // single load path for its result.
    QObject::connect(&dlg, &RasterProcessingDialogBase::resultReadyForDisplay,
                     this, &QgisDesktopWindow::loadRasterLayer);
    dlg.exec();
}

void QgisDesktopWindow::openSpectralLibraryDialog()
{
    SpectralLibraryDialog dlg(this);
    if (m_spectralProfile && m_spectralProfile->hasData())
    {
        dlg.setSpectrum(m_spectralProfile->values(),
                        m_spectralProfile->wavelengths(),
                        m_spectralProfile->bandLabels());
    }
    dlg.exec();
}

void QgisDesktopWindow::activateRoiSpectrumTool()
{
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer)
    {
        QMessageBox::information(this, tr("ROI 均值谱"),
                                 tr("请先选择一个栅格图层。"));
        return;
    }
    if (!m_mapCanvas || !m_identifyTool)
        return;

    // The tool computes the ROI mean spectrum and reports it into the Spectral
    // Profile dock; afterwards the canvas returns to the identify tool. The
    // callback is the tool's sole owner — it always restores the tool and
    // releases (empty values carry an error message in layerName).
    m_roiSpectrumTool = new RsRoiSpectrumTool(
      m_mapCanvas, rasterLayer,
      [this](const QVector<double> &values, const QVector<double> &wavelengths,
             const QVector<QString> &labels, const QString &layerName)
      {
        if (!values.isEmpty() && m_spectralProfile)
        {
          m_spectralProfile->setSpectrum(values, wavelengths, labels, layerName);
        }
        else if (!layerName.isEmpty() && statusBar())
        {
          statusBar()->showMessage(layerName, 4000);
        }
        if (m_mapCanvas && m_identifyTool)
          m_mapCanvas->setMapTool(m_identifyTool);
        // Safe asynchronous deletion: we are inside the tool's own callback.
        if (m_roiSpectrumTool)
          m_roiSpectrumTool->deleteLater();
      });

    m_mapCanvas->setMapTool(m_roiSpectrumTool.data());
}

// ---------------------------------------------------------------------------
// Contrast Stretch dialog (processing — writes a new GeoTIFF)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openContrastStretchDialog()
{
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Contrast Stretch"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<ContrastStretchDialog>(this, tr("Contrast Stretch"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Display stretch panel (renderer only — same idea as layer properties symbology)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openDisplayStretchPanel()
{
    QgsRasterLayer *rasterLayer = findActiveRaster( this );
    if ( !rasterLayer )
        rasterLayer = findAnyRaster( this );
    if ( !rasterLayer )
    {
        QMessageBox::information( this, tr( "显示拉伸" ),
                                  tr( "请先选择或加载一个栅格图层。\n"
                                      "此功能仅调整地图显示对比度，不导出新文件。" ) );
        return;
    }

    if ( !m_histogramStretchDock || !m_histogramStretch )
    {
        QMessageBox::warning( this, tr( "显示拉伸" ),
                              tr( "显示拉伸面板尚未初始化。" ) );
        return;
    }

    m_histogramStretch->setRasterLayer( rasterLayer );
    m_histogramStretchDock->setWindowTitle(
      tr( "显示拉伸 — %1" ).arg( rasterLayer->name() ) );
    m_histogramStretchDock->show();
    m_histogramStretchDock->raise();
    statusBar()->showMessage(
      tr( "显示拉伸：修改图层渲染器，仅影响显示，不写出新栅格。" ), 5000 );
}

// ---------------------------------------------------------------------------
// Spatial Filter dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpatialFilterDialog()
{
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spatial Filter"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<SpatialFilterDialog>(this, tr("Spatial Filter"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Speckle Filter (SAR) dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpeckleFilterDialog()
{
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Speckle Filter"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<SpeckleFilterDialog>(this, tr("Speckle Filter"), rasterLayer);
}

// ---------------------------------------------------------------------------
// PCA dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openPcaDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.pca" ) );
        return;
    }
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("PCA"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<PcaDialog>(this, tr("PCA"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Band Ratio / IHS dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBandRatioDialog()
{
    QgsRasterLayer *rasterLayer = findActiveRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Ratio / IHS"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<BandRatioDialog>(this, tr("Band Ratio / IHS"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Terrain Analysis dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openTerrainDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.terrain_analysis" ) );
        return;
    }
    openRasterDialog<TerrainDialog>(this, tr("Terrain Analysis"));
}

// ---------------------------------------------------------------------------
// Image Fusion dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openFusionDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.image_fusion" ) );
        return;
    }
    openRasterDialog<FusionDialog>(this, tr("Image Fusion"));
}

// ---------------------------------------------------------------------------
// Mosaic dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openMosaicDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.mosaic" ) );
        return;
    }
    openRasterDialog<MosaicDialog>(this, tr("Mosaic"));
}

// ---------------------------------------------------------------------------
// Temporal Analysis dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openTemporalAnalysisDialog()
{
    TemporalAnalysisDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && dialog.wantsRasterLoad())
    {
        const QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Change Detection dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openChangeDetectionDialog()
{
    if ( m_sessionController )
    {
        openWorkflowTool( QStringLiteral( "tool.rs.change_detection" ) );
        return;
    }
    ChangeDetectionDialog dialog(this);
    dialog.populateLayers();
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// CRS Preset dialog (project CRS)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openCrsPresetDialog()
{
    CrsPresetDialog dlg( this );
    if ( dlg.exec() == QDialog::Accepted )
    {
        int epsg = dlg.selectedEpsg();
        if ( epsg > 0 )
        {
            QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId( epsg );
            if ( crs.isValid() )
            {
                QgsProject::instance()->setCrs( crs );
                m_mapCanvas->setDestinationCrs( crs );
                m_mapCanvas->refresh();
                updateCrsDisplay();
                statusBar()->showMessage( tr( "Project CRS set to: %1" ).arg( crs.authid() ), 3000 );
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Set Layer CRS from Preset
// ---------------------------------------------------------------------------

void QgisDesktopWindow::setLayerCrsFromPreset()
{
    QList<QgsMapLayer *> selected = selectedLayers();
    if ( selected.isEmpty() )
    {
        QMessageBox::information( this, tr( "Set Layer CRS" ), tr( "No layer selected." ) );
        return;
    }

    QgsMapLayer *layer = selected.first();

    CrsPresetDialog dlg( this );
    if ( dlg.exec() == QDialog::Accepted )
    {
        int epsg = dlg.selectedEpsg();
        if ( epsg > 0 )
        {
            QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId( epsg );
            if ( crs.isValid() )
            {
                layer->setCrs( crs );
                m_mapCanvas->refresh();
                statusBar()->showMessage(
                    tr( "Layer CRS set to: %1 for \"%2\"" ).arg( crs.authid(), layer->name() ), 3000 );
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Comparison dialog (View > Compare Layers)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openComparisonDialog()
{
    ComparisonDialog dialog(this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Swipe comparison tool (View > Swipe Layers)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::toggleSwipeTool()
{
    if (!m_mapCanvas)
        return;

    if (m_mapCanvas->mapTool() == m_swipeTool) {
        // Deactivate: restore pan tool
        m_mapCanvas->setMapTool(m_panTool);
        statusBar()->showMessage(tr("Swipe tool deactivated"), 2000);
        return;
    }

    // Lazily create the swipe tool
    if (!m_swipeTool) {
        m_swipeTool = new SwipeMapTool(m_mapCanvas);
    }

    // Use current layer as base and first other visible raster as compare
    QgsMapLayer *baseLayer = m_mapCanvas->currentLayer();
    QgsMapLayer *compareLayer = nullptr;

    const auto layers = m_mapCanvas->layers();
    for (QgsMapLayer *layer : layers) {
        if (layer && layer != baseLayer && layer->type() == Qgis::LayerType::Raster) {
            compareLayer = layer;
            break;
        }
    }

    if (!baseLayer || !compareLayer) {
        statusBar()->showMessage(tr("Swipe requires at least two raster layers"), 3000);
        return;
    }

    m_swipeTool->setBaseLayer(baseLayer);
    m_swipeTool->setCompareLayer(compareLayer);
    m_mapCanvas->setMapTool(m_swipeTool);
    statusBar()->showMessage(tr("Swipe tool active — move mouse to compare '%1' vs '%2'")
                                 .arg(baseLayer->name(), compareLayer->name()), 3000);
}

// ---------------------------------------------------------------------------
// Batch processing dialog (Processing > Batch Processing)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBatchProcessingDialog()
{
    BatchProcessingDialog dialog(this);
    dialog.exec();
}
