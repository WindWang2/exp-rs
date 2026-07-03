// main_window_project.cpp — Project I/O and data import
#include "main_window.h"

#include "dialogs/stac_browser_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <layout/qgsprintlayout.h>
#include <layout/qgslayoutmanager.h>
#include <layout/qgslayoutdesignerdialog.h>

// ── Project Actions ────────────────────────────────────────────────────────

void QgisDesktopWindow::newProject()
{
    if (!checkUnsavedChanges())
        return;

    QgsProject::instance()->clear();
    m_mapCanvas->setLayers({});
    m_mapCanvas->refresh();
    updateEditingUI(nullptr);
    statusBar()->showMessage("New project created", 3000);
}

void QgisDesktopWindow::newLayout()
{
    // Create a new print layout and register with project layout manager
    QgsPrintLayout *layout = new QgsPrintLayout( QgsProject::instance() );
    layout->initializeDefaults();
    QgsProject::instance()->layoutManager()->addLayout( layout );

    // Create and show the layout designer
    auto *designer = new QgsLayoutDesignerDialog( layout, this );
    designer->window()->setAttribute( Qt::WA_DeleteOnClose );
    designer->window()->show();
}

void QgisDesktopWindow::openProject()
{
    if (!checkUnsavedChanges())
        return;

    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Project", "",
        "QGIS Projects (*.qgs *.qgz);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        QgsProject::instance()->read(filePath);
        refreshCanvasLayers();
        updateCrsDisplay();
        updateEditingUI(currentVectorLayer());
        statusBar()->showMessage(QString("Opened project: %1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::saveProject()
{
    if (QgsProject::instance()->fileName().isEmpty()) {
        saveProjectAs();
    } else {
        QgsProject::instance()->write();
        statusBar()->showMessage("Project saved", 3000);
    }
}

void QgisDesktopWindow::saveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, "Save Project", "",
        "QGIS Projects (*.qgs);;All Files (*.*)"
    );
    if (!filePath.isEmpty()) {
        QgsProject::instance()->write(filePath);
        statusBar()->showMessage(QString("Saved project: %1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::importLayer()
{
    // Support both raster and vector files
    QString filter = tr( "All supported files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.asc *.shp *.gpkg *.geojson *.kml *.gml);;"
                         "Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.asc);;"
                         "Vector files (*.shp *.gpkg *.geojson *.kml *.gml);;"
                         "All files (*)" );
    QString path = QFileDialog::getOpenFileName( this, tr( "Import Layer" ), QString(), filter );
    if ( path.isEmpty() )
        return;

    QString ext = QFileInfo( path ).suffix().toLower();
    QStringList rasterExts = { "tif", "tiff", "img", "jp2", "png", "jpg", "asc" };
    if ( rasterExts.contains( ext ) )
        loadRasterLayer( path );
    else
        loadVectorLayer( path );
}

void QgisDesktopWindow::browseStacCatalog()
{
    StacBrowserDialog dlg(m_mapCanvas, this);
    dlg.exec();
}
