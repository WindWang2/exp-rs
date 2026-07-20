// main_window_view.cpp — Map view and navigation actions
#include "main_window.h"

#include <QMessageBox>
#include <QStatusBar>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <georeferencer/qgsgeoreferencermainwindow.h>
#include <georeferencer/qgsgeoref_image_to_map_window.h>

#ifdef SICNU_HAS_CLASSIFY
#include "classification/qgsclassificationmainwindow.h"
#endif
#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
#endif

// ── View Actions ──────────────────────────────────────────────────────────
void QgisDesktopWindow::zoomIn() { m_mapCanvas->zoomIn(); }
void QgisDesktopWindow::zoomOut() { m_mapCanvas->zoomOut(); }
void QgisDesktopWindow::panMap() { m_mapCanvas->setMapTool(m_panTool); }
void QgisDesktopWindow::identifyFeatures() { m_mapCanvas->setMapTool(m_identifyTool); }

void QgisDesktopWindow::measureDistance()
{
    m_mapCanvas->setMapTool( m_measureDistanceTool );
    statusBar()->showMessage( tr( "Measure Distance: click to add points, double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::measureArea()
{
    m_mapCanvas->setMapTool( m_measureAreaTool );
    statusBar()->showMessage( tr( "Measure Area: click to add points, double-click or right-click to finish" ), 5000 );
}

void QgisDesktopWindow::openGeoreferencer()
{
    openGeorefImageToImage();
}

void QgisDesktopWindow::openGeorefImageToImage()
{
    if ( !m_georefI2I )
    {
        m_georefI2I = new QgsGeoreferencerMainWindow( nullptr, this );
        m_georefI2I->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2I->setWindowTitle( tr( "Image Registration · Image 2 Image" ) );
    }
    m_georefI2I->show();
    m_georefI2I->raise();
    m_georefI2I->activateWindow();
}

void QgisDesktopWindow::openGeorefImageToMap()
{
    if ( !m_georefI2M )
    {
        m_georefI2M = new QgsGeorefImageToMapWindow( nullptr, this );
        m_georefI2M->setAttribute( Qt::WA_DeleteOnClose, false );
        m_georefI2M->setWindowTitle( tr( "Image Registration · Image 2 Map" ) );
    }
    m_georefI2M->show();
    m_georefI2M->raise();
    m_georefI2M->activateWindow();
}

#ifdef SICNU_HAS_CLASSIFY
void QgisDesktopWindow::openClassificationWindow()
{
    if ( !m_classifyWindow )
    {
        // iface = nullptr for now; later tasks may pass a real QgisInterface.
        m_classifyWindow = new QgsClassificationMainWindow( nullptr, this );
        m_classifyWindow->setAttribute( Qt::WA_DeleteOnClose, false );
    }
    m_classifyWindow->show();
    m_classifyWindow->raise();
    m_classifyWindow->activateWindow();
}
#else
void QgisDesktopWindow::openClassificationWindow() {
    QMessageBox::information(this, tr("Classification"),
        tr("Supervised classification requires OpenCV with the ml module.\n"
           "Install opencv (including opencv-ml) and rebuild:\n"
           "  cd build && cmake .. && make -j$(nproc)"));
}
#endif

#ifdef SICNU_HAS_OBIA
#include "rs_obia_main_window.h"
void QgisDesktopWindow::openObiaWindow()
{
    if ( !m_obiaWindow )
    {
        m_obiaWindow = new RsObiaMainWindow( this );
        m_obiaWindow->setAttribute( Qt::WA_DeleteOnClose, false );
    }
    m_obiaWindow->show();
    m_obiaWindow->raise();
    m_obiaWindow->activateWindow();
}
#else
void QgisDesktopWindow::openObiaWindow() {
    QMessageBox::information(this, tr("OBIA"),
        tr("Object-based classification requires OpenCV ml module.\n"
           "Build with SICNU_HAS_OBIA=ON to enable this feature."));
}
#endif


void QgisDesktopWindow::zoomFullExtent()
{
    m_mapCanvas->zoomToFullExtent();
    statusBar()->showMessage("Full extent", 2000);
}

void QgisDesktopWindow::zoomToLayer()
{
    QList<QgsMapLayer*> selected = selectedLayers();
    if (!selected.isEmpty()) {
        m_mapCanvas->setExtent(selected.first()->extent());
        m_mapCanvas->refresh();
        statusBar()->showMessage("Zoomed to layer", 2000);
    }
}

void QgisDesktopWindow::refreshMap()
{
    m_mapCanvas->refresh();
    statusBar()->showMessage("Map refreshed", 2000);
}
