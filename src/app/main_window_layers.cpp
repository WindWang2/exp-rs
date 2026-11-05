// main_window_layers.cpp — Layer management and identify results
#include "main_window.h"

#include "active_view_host.h"
#include "app_paths.h"
#include "dialogs/crs_preset_dialog.h"
#include "dialogs/import_batch_dialog.h"
#include "project_context.h"
#include "panels/data_manager_panel.h"
#include "widgets/spectral_profile_widget.h"

#include <QDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextBrowser>

#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsfields.h>
#include <qgsfeature.h>
#include <qgsmaptoolidentify.h>
#include <qgscoordinatereferencesystem.h>
#include <qgslayertreegroup.h>
#include <qgis.h>

void QgisDesktopWindow::onIdentifyResults(const QList<QgsMapToolIdentify::IdentifyResult> &results)
{
    if (results.isEmpty())
    {
        m_identifyResults->setHtml(QStringLiteral(
            "<html><body style='font-family:sans-serif; padding:8px;'>"
            "<p style='color:#888;'>%1</p>"
            "</body></html>"
        ).arg(tr("No features found at this location.")));
        if (m_identifyDock)
            m_identifyDock->raise();
        return;
    }

    QString html;
    html.reserve(4096);
    html += QStringLiteral(
        "<html><body style='font-family:sans-serif; padding:4px;'>"
    );

    for (const QgsMapToolIdentify::IdentifyResult &result : results)
    {
        // Layer name
        const QString layerName = result.mLayer
            ? result.mLayer->name()
            : tr("Unknown Layer");

        html += QStringLiteral("<h3 style='margin-bottom:2px;'>%1</h3>").arg(layerName.toHtmlEscaped());

        // Determine if this is a raster or vector result
        const bool isRaster = result.mLayer
            && result.mLayer->type() == Qgis::LayerType::Raster;

        html += QStringLiteral(
            "<table style='border-collapse:collapse; width:100%;'>"
        );

        if (isRaster)
        {
            // Raster: show label (pixel value / band info) and attributes
            if (!result.mLabel.isEmpty())
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(tr("Value"), result.mLabel.toHtmlEscaped());
            }

            // Raster attributes (band values)
            for (auto it = result.mAttributes.constBegin(); it != result.mAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }

            // Derived attributes (coordinates, etc.)
            for (auto it = result.mDerivedAttributes.constBegin(); it != result.mDerivedAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }
        }
        else
        {
            // Vector: show feature attributes from fields
            const QgsFields fields = result.mFields;
            const QgsFeature feature = result.mFeature;

            if (fields.isEmpty() && !result.mAttributes.isEmpty())
            {
                // Fallback: use mAttributes map (e.g. for vector tile layers)
                for (auto it = result.mAttributes.constBegin(); it != result.mAttributes.constEnd(); ++it)
                {
                    html += QStringLiteral(
                        "<tr style='border-bottom:1px solid #ddd;'>"
                        "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                        "<td style='padding:3px 8px;'>%2</td>"
                        "</tr>"
                    ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
                }
            }
            else if (!fields.isEmpty())
            {
                // Show field name + value pairs from the feature
                for (int i = 0; i < fields.count(); ++i)
                {
                    const QString fieldName = fields.at(i).name();
                    const QString fieldValue = feature.attribute(i).toString();
                    html += QStringLiteral(
                        "<tr style='border-bottom:1px solid #ddd;'>"
                        "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                        "<td style='padding:3px 8px;'>%2</td>"
                        "</tr>"
                    ).arg(fieldName.toHtmlEscaped(), fieldValue.toHtmlEscaped());
                }
            }

            // Derived attributes (coordinates, area, etc.)
            for (auto it = result.mDerivedAttributes.constBegin(); it != result.mDerivedAttributes.constEnd(); ++it)
            {
                html += QStringLiteral(
                    "<tr style='border-bottom:1px solid #ddd;'>"
                    "<td style='padding:3px 8px; font-weight:bold; white-space:nowrap;'>%1</td>"
                    "<td style='padding:3px 8px;'>%2</td>"
                    "</tr>"
                ).arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
            }
        }

        html += QStringLiteral("</table><br/>");
    }

    html += QStringLiteral("</body></html>");

    m_identifyResults->setHtml(html);

    // Raise the dock so the user can see results
    if (m_identifyDock)
        m_identifyDock->raise();

    // Update spectral profile for the first raster layer result
    if (m_spectralProfile)
    {
        QgsRasterLayer *rasterLayer = nullptr;
        for (const QgsMapToolIdentify::IdentifyResult &result : results)
        {
            if (result.mLayer && result.mLayer->type() == Qgis::LayerType::Raster)
            {
                rasterLayer = qobject_cast<QgsRasterLayer *>(result.mLayer);
                if (rasterLayer)
                    break;
            }
        }

        if (rasterLayer && m_identifyTool)
        {
            QgsPointXY clickedPoint = m_identifyTool->lastClickedPoint();
            m_spectralProfile->setProfile(clickedPoint, rasterLayer);
        }
    }
}
void QgisDesktopWindow::addRasterLayer()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr( "Open Raster Layers (multi-select)" ),
      AppPaths::dataDir(),
      tr( "Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;"
          "ENVI raster (*.dat *.hdr *.img *.bil *.bsq *.bip);;"
          "All files (*)" ) );
    if ( paths.isEmpty() || !m_activeViewHost )
      return;

    importLayerBatch( paths, /*raster=*/ true );
}

void QgisDesktopWindow::addVectorLayer()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr( "Open Vector Layers (multi-select)" ),
      AppPaths::dataDir(),
      tr( "Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)" ) );
    if ( paths.isEmpty() || !m_activeViewHost )
      return;

    importLayerBatch( paths, /*raster=*/ false );
}

void QgisDesktopWindow::importLayerBatch( const QStringList &paths, bool raster )
{
    QStringList sources;
    for ( const QString &filePath : paths )
      if ( !filePath.isEmpty() )
        sources.append( filePath );

    // WP03 (R28-T04): a multi-file batch goes through the import review —
    // T02's ImportPreflight/ImportSession state rendered as one row per
    // source with typed refusal reasons, needs-choice handling and the
    // dedup explanation, instead of a bare "N succeeded, M failed" status
    // line. Sources left pending by an abandoned batch ride along (the
    // catalog's own dedup makes re-offering them idempotent — that IS the
    // resume entry).
    sicnu::data::DataManager *manager =
      m_projectContext ? &m_projectContext->dataManager() : nullptr;
    if ( sources.size() >= 2 && manager )
    {
      const QStringList resumable =
        sicnu::app::ImportBatchDialog::pendingPathsFromJournal(
          sicnu::app::ImportBatchDialog::journalPath() );
      sicnu::app::ImportBatchDialog dialog( manager, sources + resumable, this );
      dialog.runCheck();
      if ( dialog.exec() != QDialog::Accepted )
      {
        statusBar()->showMessage(
          tr( "Import batch cancelled — refused or pending items were not registered." ),
          4000 );
        return;
      }
      int ok = 0;
      int failed = 0;
      for ( const QString &registered : dialog.registeredPaths() )
      {
        const bool displayed = raster ? m_activeViewHost->openRasterPath( registered )
                                      : m_activeViewHost->openVectorPath( registered );
        if ( displayed )
          ++ok;
        else
          ++failed;
      }
      if ( m_dataManagerPanel )
      {
        m_dataManagerPanel->show();
        m_dataManagerPanel->raise();
      }
      statusBar()->showMessage(
        tr( "Imported %1 of %2 source(s)%3" )
          .arg( dialog.registeredPaths().size() )
          .arg( sources.size() + resumable.size() )
          .arg( failed == 0 ? QString()
                            : tr( " (%1 could not be displayed)" ).arg( failed ) ),
        5000 );
      return;
    }

    // Single source (or no catalog): the direct open path stays.
    int ok = 0;
    int failed = 0;
    for ( const QString &filePath : sources )
    {
      const bool displayed = raster ? m_activeViewHost->openRasterPath( filePath )
                                    : m_activeViewHost->openVectorPath( filePath );
      if ( displayed )
        ++ok;
      else
        ++failed;
    }
    if ( m_dataManagerPanel )
    {
      m_dataManagerPanel->show();
      m_dataManagerPanel->raise();
    }
    statusBar()->showMessage(
      raster ? ( failed == 0 ? tr( "Loaded %1 rasters" ).arg( ok )
                             : tr( "Raster loading: %1 succeeded, %2 failed" ).arg( ok ).arg( failed ) )
             : ( failed == 0 ? tr( "Loaded %1 vectors" ).arg( ok )
                             : tr( "Vector loading: %1 succeeded, %2 failed" ).arg( ok ).arg( failed ) ),
      4000 );
}

void QgisDesktopWindow::layerProperties()
{
    QList<QgsMapLayer*> selected = m_activeViewHost->selectedLayers();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Layer Properties"), tr("No layer selected"));
        return;
    }

    QgsMapLayer *layer = selected.first();
    m_activeViewHost->showLayerProperties(layer);
}

void QgisDesktopWindow::removeLayer()
{
    m_activeViewHost->removeSelectedDisplayLayers();
}

void QgisDesktopWindow::setProjectCrs()
{
    CrsPresetDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        int epsg = dlg.selectedEpsg();
        if (epsg > 0)
        {
            QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId(epsg);
            if (crs.isValid())
            {
                QgsProject::instance()->setCrs(crs);
                m_mapCanvas->setDestinationCrs(crs);
                m_mapCanvas->refresh();
                updateCrsDisplay();
                statusBar()->showMessage(tr("Project CRS set to: %1").arg(crs.authid()), 3000);
            }
        }
    }
}
QgsLayerTreeGroup *QgisDesktopWindow::findOrCreateGroup(const QString &name)
{
    return m_activeViewHost->findOrCreateGroup(name);
}

// ── Layer Loading (delegated to ActiveViewHost / active Display View) ─────
bool QgisDesktopWindow::loadRasterLayer(const QString &filePath)
{
    return m_activeViewHost && static_cast<bool>(m_activeViewHost->openRasterPath(filePath));
}

bool QgisDesktopWindow::loadVectorLayer(const QString &filePath)
{
    return m_activeViewHost && static_cast<bool>(m_activeViewHost->openVectorPath(filePath));
}

void QgisDesktopWindow::showLayerProperties(QgsMapLayer *layer)
{
    m_activeViewHost->showLayerProperties(layer);
}

void QgisDesktopWindow::refreshCanvasLayers()
{
    m_activeViewHost->refreshCanvasLayers();
}

QgsMapLayer *QgisDesktopWindow::activeLayer()
{
    return m_activeViewHost->activeLayer();
}

QList<QgsMapLayer*> QgisDesktopWindow::selectedLayers()
{
    return m_activeViewHost->selectedLayers();
}

bool QgisDesktopWindow::loadDataLayer( const QString &filePath )
{
    if ( !m_activeViewHost )
        return false;
    return static_cast<bool>( m_activeViewHost->openPath( filePath ) );
}
