// main_window_view.cpp — Map view and navigation actions
#include "main_window.h"

#include <QMessageBox>
#include <QStatusBar>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <georeferencer/qgsgeoreferencermainwindow.h>

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
    if ( !m_georefWindow )
    {
        // iface = nullptr for now; Task 11.4.7 will pass a real QgisInterface.
        m_georefWindow = new QgsGeoreferencerMainWindow( nullptr, this );
        m_georefWindow->setAttribute( Qt::WA_DeleteOnClose, false );
    }
    m_georefWindow->show();
    m_georefWindow->raise();
    m_georefWindow->activateWindow();
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
