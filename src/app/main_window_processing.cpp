// main_window_processing.cpp — RS processing dialog slots
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

#include "dialogs/image_enhancement_panel.h"
#include "dialogs/band_math_dialog.h"
#include "dialogs/spectral_index_dialog.h"
#include "dialogs/atmospheric_dialog.h"
#include "dialogs/contrast_stretch_dialog.h"
#include "dialogs/spatial_filter_dialog.h"
#include "dialogs/speckle_filter_dialog.h"
#include "dialogs/pca_dialog.h"
#include "dialogs/band_ratio_dialog.h"
#include "dialogs/terrain_dialog.h"
#include "dialogs/fusion_dialog.h"
#include "dialogs/mosaic_dialog.h"
#include "dialogs/change_detection_dialog.h"
#include "dialogs/crs_preset_dialog.h"
#include "dialogs/comparison_dialog.h"
#include "dialogs/batch_processing_dialog.h"
#include "dialogs/sicnu_algorithm_dialog.h"

#include <qgsapplication.h>
#include <qgsproject.h>
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
    ImageEnhancementPanel dialog(this);
    QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(activeLayer());
    if (rasterLayer) {
        dialog.setRasterLayer(rasterLayer);
    }
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Band Math dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBandMathDialog()
{
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
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spectral Index"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<SpectralIndexDialog>(this, tr("Spectral Index"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Atmospheric Correction dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openAtmosphericCorrectionDialog()
{
    QgsRasterLayer *rasterLayer = findAnyRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Atmospheric Correction"),
                                 tr("Please select a raster layer first."));
        return;
    }
    openRasterDialog<AtmosphericDialog>(this, tr("Atmospheric Correction"), rasterLayer);
}

// ---------------------------------------------------------------------------
// Contrast Stretch dialog
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
    openRasterDialog<TerrainDialog>(this, tr("Terrain Analysis"));
}

// ---------------------------------------------------------------------------
// Image Fusion dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openFusionDialog()
{
    openRasterDialog<FusionDialog>(this, tr("Image Fusion"));
}

// ---------------------------------------------------------------------------
// Mosaic dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openMosaicDialog()
{
    openRasterDialog<MosaicDialog>(this, tr("Mosaic"));
}

// ---------------------------------------------------------------------------
// Change Detection dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openChangeDetectionDialog()
{
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
// Batch processing dialog (Processing > Batch Processing)
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBatchProcessingDialog()
{
    BatchProcessingDialog dialog(this);
    dialog.exec();
}
