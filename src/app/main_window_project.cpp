// main_window_project.cpp — Project I/O and data import
#include "main_window.h"

#include "app_paths.h"
#include "active_view_host.h"
#include "project_context.h"
#include "dialogs/stac_browser_dialog.h"
#include "dialogs/product_import_dialog.h"
#include "panels/data_manager_panel.h"
#include "operators/framework/rs_operation_logger.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QStringList>

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

    if ( !m_projectContext )
    {
        QMessageBox::warning( this, tr( "New Project" ),
                              tr( "The project Data Context is unavailable." ) );
        return;
    }

    const auto cleared =
        m_projectContext->clearProject( *QgsProject::instance() );
    if ( !cleared )
    {
        QStringList details;
        for ( const auto &diagnostic : cleared.diagnostics() )
            details.append( QStringLiteral( "[%1] %2" )
                                .arg( diagnostic.code, diagnostic.message ) );
        QMessageBox::warning(
            this, tr( "New Project" ),
            tr( "Failed to clear the project data context:\n%1" )
                .arg( details.join( '\n' ) ) );
        return;
    }

    m_mapCanvas->setLayers({});
    m_mapCanvas->refresh();
    updateCanvasEmptyState();
    updateLayersEmptyState();
    updateEditingUI(nullptr);
    statusBar()->showMessage(tr("已新建工程"), 3000);
}

void QgisDesktopWindow::newLayout()
{
    // Create a new print layout and register with project layout manager
    QgsPrintLayout *layout = new QgsPrintLayout( QgsProject::instance() );
    layout->initializeDefaults();
    QgsProject::instance()->layoutManager()->addLayout( layout );

    // Create and show the layout designer
    auto *designer = new QgsLayoutDesignerDialog( layout, m_mapCanvas, this );
    designer->window()->setAttribute( Qt::WA_DeleteOnClose );
    designer->window()->show();
}

void QgisDesktopWindow::openProject()
{
    if (!checkUnsavedChanges())
        return;

    QString filePath = QFileDialog::getOpenFileName(
        this, tr("打开工程"), "",
        tr("QGIS 工程文件 (*.qgs *.qgz);;所有文件 (*.*)")
    );
    if (!filePath.isEmpty()) {
        if ( !m_projectContext )
        {
            QMessageBox::warning(
                this, tr( "Open Project" ),
                tr( "The project Data Context is unavailable." ) );
            return;
        }

        const auto cleared =
            m_projectContext->clearProject( *QgsProject::instance() );
        if ( !cleared )
        {
            QStringList details;
            for ( const auto &diagnostic : cleared.diagnostics() )
                details.append( QStringLiteral( "[%1] %2" )
                                    .arg( diagnostic.code,
                                          diagnostic.message ) );
            QMessageBox::warning(
                this, tr( "Open Project" ),
                tr( "Failed to release the current project data:\n%1" )
                    .arg( details.join( '\n' ) ) );
            return;
        }

        if ( !QgsProject::instance()->read(filePath) )
        {
            QMessageBox::warning(
                this, tr( "Open Project" ),
                tr( "Failed to open project:\n%1" ).arg( filePath ) );
            return;
        }
        refreshCanvasLayers();
        updateCanvasEmptyState();
        updateLayersEmptyState();
        updateCrsDisplay();
        updateEditingUI(currentVectorLayer());
        statusBar()->showMessage(tr("已打开工程：%1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::saveProject()
{
    if (QgsProject::instance()->fileName().isEmpty()) {
        saveProjectAs();
    } else {
        QgsProject::instance()->write();
        statusBar()->showMessage(tr("工程已保存"), 3000);
    }
}

void QgisDesktopWindow::saveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存工程"), "",
        tr("QGIS 工程文件 (*.qgs);;所有文件 (*.*)")
    );
    if (!filePath.isEmpty()) {
        QgsProject::instance()->write(filePath);
        statusBar()->showMessage(tr("工程已保存至：%1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::importLayer()
{
    // Raster includes ENVI (.dat / .hdr; extensionless binaries via All files)
    const QString filter = tr(
      "All supported files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip *.shp *.gpkg *.geojson *.kml *.gml);;"
      "Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;"
      "ENVI raster (*.dat *.hdr *.img *.bil *.bsq *.bip);;"
      "Vector files (*.shp *.gpkg *.geojson *.kml *.gml);;"
      "All files (*)" );
    const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr( "导入数据（可多选）" ), AppPaths::dataDir(), filter );
    if ( paths.isEmpty() || !m_activeViewHost )
      return;

    int ok = 0;
    int failed = 0;
    for ( const QString &path : paths )
    {
      if ( path.isEmpty() )
        continue;
      if ( m_activeViewHost->loadLayer( path ) )
        ++ok;
      else
        ++failed;
    }

    if ( m_dataManagerPanel )
    {
      m_dataManagerPanel->show();
      m_dataManagerPanel->raise();
      m_dataManagerPanel->refresh();
    }

    if ( failed == 0 )
    {
      statusBar()->showMessage(
        tr( "已导入 %1 个文件" ).arg( ok ), 4000 );
    }
    else
    {
      statusBar()->showMessage(
        tr( "导入完成：成功 %1，失败 %2" ).arg( ok ).arg( failed ), 6000 );
    }
}

void QgisDesktopWindow::browseStacCatalog()
{
    StacBrowserDialog dlg(m_mapCanvas, this);
    dlg.exec();
}

void QgisDesktopWindow::openProductImportDialog(const QString &family)
{
    if ( !m_projectContext )
    {
        QMessageBox::information( this, tr( "导入产品" ),
                                  tr( "工程数据上下文不可用。" ) );
        return;
    }

    ProductImportDialog dialog( this );
    dialog.setDataManager( &m_projectContext->dataManager() );
    dialog.setProductFamily( family );
    // The dialog runs the probe-preview-commit transaction itself; on accept
    // the collection + selected band children are registered and the Data
    // Manager panel refreshes via collectionAdded/assetAdded.
    dialog.exec();
}

void QgisDesktopWindow::exportLabReport()
{
    auto& logger = sicnu::operators::RSOperationLogger::instance();
    if (logger.recordCount() == 0) {
        QMessageBox::information(this, tr("Export Lab Report"),
                                 tr("No operations have been recorded yet."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export Lab Report"), QString(),
        tr("JSON files (*.json);;CSV files (*.csv);;All files (*.*)"));
    if (filePath.isEmpty())
        return;

    std::string errorMessage;
    if (!logger.exportToFile(filePath.toStdString(), &errorMessage)) {
        QMessageBox::warning(this, tr("Export Lab Report"),
                             tr("Failed to export report:\n%1")
                                 .arg(QString::fromStdString(errorMessage)));
        return;
    }

    statusBar()->showMessage(tr("Lab report exported: %1").arg(filePath), 3000);
}
