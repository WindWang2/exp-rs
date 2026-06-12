// main_window_processing.cpp — RS processing dialog slots
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

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
// Band Math dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBandMathDialog()
{
    QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(activeLayer());
    if (!rasterLayer) {
        for (QgsMapLayer *layer : QgsProject::instance()->mapLayers().values()) {
            if (layer->type() == Qgis::LayerType::Raster) {
                rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
                break;
            }
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Math"),
                                 tr("Please select a raster layer first."));
        return;
    }

    BandMathDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Spectral Index dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpectralIndexDialog()
{
    QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(activeLayer());
    if (!rasterLayer) {
        for (QgsMapLayer *layer : QgsProject::instance()->mapLayers().values()) {
            if (layer->type() == Qgis::LayerType::Raster) {
                rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
                break;
            }
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spectral Index"),
                                 tr("Please select a raster layer first."));
        return;
    }

    SpectralIndexDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
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
    QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer*>(activeLayer());
    if (!rasterLayer) {
        for (QgsMapLayer *layer : QgsProject::instance()->mapLayers().values()) {
            if (layer->type() == Qgis::LayerType::Raster) {
                rasterLayer = qobject_cast<QgsRasterLayer*>(layer);
                break;
            }
        }
    }
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Atmospheric Correction"),
                                 tr("Please select a raster layer first."));
        return;
    }

    AtmosphericDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Helper: find selected raster layer
// ---------------------------------------------------------------------------

static QgsRasterLayer *findSelectedRaster(QgisDesktopWindow *win)
{
    QList<QgsMapLayer*> layers = win->selectedLayers();
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster)
            return qobject_cast<QgsRasterLayer*>(layer);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Contrast Stretch dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openContrastStretchDialog()
{
    QgsRasterLayer *rasterLayer = findSelectedRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Contrast Stretch"),
                                 tr("Please select a raster layer first."));
        return;
    }
    ContrastStretchDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Spatial Filter dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpatialFilterDialog()
{
    QgsRasterLayer *rasterLayer = findSelectedRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Spatial Filter"),
                                 tr("Please select a raster layer first."));
        return;
    }
    SpatialFilterDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Speckle Filter (SAR) dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openSpeckleFilterDialog()
{
    QgsRasterLayer *rasterLayer = findSelectedRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Speckle Filter"),
                                 tr("Please select a raster layer first."));
        return;
    }
    SpeckleFilterDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// PCA dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openPcaDialog()
{
    QgsRasterLayer *rasterLayer = findSelectedRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("PCA"),
                                 tr("Please select a raster layer first."));
        return;
    }
    PcaDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Band Ratio / IHS dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openBandRatioDialog()
{
    QgsRasterLayer *rasterLayer = findSelectedRaster(this);
    if (!rasterLayer) {
        QMessageBox::information(this, tr("Band Ratio / IHS"),
                                 tr("Please select a raster layer first."));
        return;
    }
    BandRatioDialog dialog(this);
    dialog.setRasterLayer(rasterLayer);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Terrain Analysis dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openTerrainDialog()
{
    TerrainDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // Output is loaded automatically by the dialog
    }
}

// ---------------------------------------------------------------------------
// Image Fusion dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openFusionDialog()
{
    FusionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
}

// ---------------------------------------------------------------------------
// Mosaic dialog
// ---------------------------------------------------------------------------

void QgisDesktopWindow::openMosaicDialog()
{
    MosaicDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString outPath = dialog.outputPath();
        if (!outPath.isEmpty() && QFile::exists(outPath))
            loadRasterLayer(outPath);
    }
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
