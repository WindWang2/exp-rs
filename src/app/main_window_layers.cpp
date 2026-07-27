// main_window_layers.cpp — Layer management and identify results
#include "main_window.h"

#include "active_view_host.h"
#include "app_paths.h"
#include "dialogs/crs_preset_dialog.h"
#include "panels/data_manager_panel.h"
#include "widgets/spectral_profile_widget.h"

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
      this, tr( "打开栅格图层（可多选）" ),
      AppPaths::dataDir(),
      tr( "Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;"
          "ENVI raster (*.dat *.hdr *.img *.bil *.bsq *.bip);;"
          "All files (*)" ) );
    if ( paths.isEmpty() || !m_activeViewHost )
      return;

    int ok = 0;
    int failed = 0;
    for ( const QString &filePath : paths )
    {
      if ( filePath.isEmpty() )
        continue;
      if ( m_activeViewHost->openRasterPath( filePath ) )
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
      failed == 0 ? tr( "已加载 %1 个栅格" ).arg( ok )
                  : tr( "栅格加载：成功 %1，失败 %2" ).arg( ok ).arg( failed ),
      4000 );
}

void QgisDesktopWindow::addVectorLayer()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr( "打开矢量图层（可多选）" ),
      AppPaths::dataDir(),
      tr( "Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)" ) );
    if ( paths.isEmpty() || !m_activeViewHost )
      return;

    int ok = 0;
    int failed = 0;
    for ( const QString &filePath : paths )
    {
      if ( filePath.isEmpty() )
        continue;
      if ( m_activeViewHost->openVectorPath( filePath ) )
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
      failed == 0 ? tr( "已加载 %1 个矢量" ).arg( ok )
                  : tr( "矢量加载：成功 %1，失败 %2" ).arg( ok ).arg( failed ),
      4000 );
}

void QgisDesktopWindow::layerProperties()
{
    QList<QgsMapLayer*> selected = m_activeViewHost->selectedLayers();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Layer Properties", "No layer selected");
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
                statusBar()->showMessage(QString("Project CRS set to: %1").arg(crs.authid()), 3000);
            }
        }
    }
}
QgsLayerTreeGroup *QgisDesktopWindow::findOrCreateGroup(const QString &name)
{
    return m_activeViewHost->findOrCreateGroup(name);
}

// ── Layer Loading (delegated to ActiveViewHost / active Display View) ─────
void QgisDesktopWindow::loadRasterLayer(const QString &filePath)
{
    m_activeViewHost->openRasterPath(filePath);
}

void QgisDesktopWindow::loadVectorLayer(const QString &filePath)
{
    m_activeViewHost->openVectorPath(filePath);
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
