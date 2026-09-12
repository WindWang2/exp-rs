// main_window_project.cpp — Project I/O and data import
#include "main_window.h"

#include "app_paths.h"
#include "active_view_host.h"
#include "experiment/bridge/execution_event_conversion.h"
#include "experiment/bridge/lab_report.h"
#include "experiment/bridge/lab_report_writers.h"
#include "experiment/bridge/lab_run_recorder.h"
#include "preview/asset_preview_service.h"
#include "project_context.h"
#include "dialogs/stac_browser_dialog.h"
#include "dialogs/product_import_dialog.h"
#include "operators/framework/rs_operation_logger.h"
#include "panels/data_manager_panel.h"
#include "workflow/workflow_run_coordinator.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>

#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <layout/qgsprintlayout.h>
#include <layout/qgslayoutmanager.h>
#include <layout/qgslayoutdesignerdialog.h>

// ── Project Actions ────────────────────────────────────────────────────────

namespace
{
// D5 lab-report wiring. The recorder is recreated per opened project: the
// bridge deliberately refuses re-binding a live recorder to a second db
// (ADR 0143), and project switches must never land one lab's runs in
// another project's experiment store.
sicnu::experiment::LabRunRecorder *s_labRecorder = nullptr;

constexpr const char *kLabAutoRecordKey = "lab/autoRecordExperimentRuns";
constexpr const char *kLabStudentKey = "lab/studentName";
constexpr const char *kLabSessionKey = "lab/sessionName";
constexpr int kMaxReportThumbnails = 8;

/// Auto-recording per ADR 0143 / goal D5: every lab execution of the opened
/// project registers an experiment run. Opt-out (never opt-in) via
/// QSettings `lab/autoRecordExperimentRuns`; a bound recorder keeps its
/// already-started stories truthful when disabled mid-flight.
void ensureLabRecordingForProject( const QString &projectPath )
{
    QSettings settings;
    if ( !settings.value( QLatin1String( kLabAutoRecordKey ), true ).toBool() )
        return;

    const QFileInfo projectInfo( projectPath );
    if ( projectInfo.fileName().isEmpty() )
        return;

    const QString dbPath =
        projectInfo.dir().filePath( QStringLiteral( ".sicnu/lab/experiments.db" ) );
    QDir().mkpath( QFileInfo( dbPath ).absolutePath() );

    if ( s_labRecorder )
    {
        // Queued events for the old instance are dropped with it; a project
        // switch is a story boundary, not a mid-run event.
        s_labRecorder->deleteLater();
        s_labRecorder = nullptr;
    }
    s_labRecorder = new sicnu::experiment::LabRunRecorder(
        sicnu::workflow::WorkflowRunCoordinator::instance(), QCoreApplication::instance() );
    // The operation trail (RSOperationLogger, jsoncpp) rides into the run
    // evidence through the injected source; the science side never links the
    // operator stack.
    s_labRecorder->setOperationTrailSource( []() {
        return sicnu::experiment::jsonCppToQJson(
                   sicnu::operators::RSOperationLogger::instance().toJson() )
            .toArray();
    } );

    QString error;
    const QString experimentId =
        QStringLiteral( "lab-%1" ).arg( projectInfo.completeBaseName() );
    if ( !s_labRecorder->enable( dbPath, experimentId, projectInfo.completeBaseName(),
                                 QStringLiteral( "课堂实验记录" ), QString(), &error ) )
    {
        s_labRecorder->deleteLater();
        s_labRecorder = nullptr;
        qWarning( "lab recording not enabled: %s", qUtf8Printable( error ) );
    }
}

void stopLabRecording()
{
    // The cleared project has no lab anymore: no NEW stories may land in the
    // previous project's store. Already-recorded runs keep their records.
    if ( s_labRecorder )
        s_labRecorder->setRecordingEnabled( false );
}

/// Thumbnails for the report, generated through the existing bounded raster
/// preview path (overview-backed, no upsampling); PNG at ≤512 px long edge.
QVector<sicnu::experiment::LabReportThumbnail> collectThumbnails( const QString &labId )
{
    QVector<sicnu::experiment::LabReportThumbnail> thumbnails;
    if ( !s_labRecorder || !s_labRecorder->store() )
        return thumbnails;

    const auto runs =
        s_labRecorder->store()->listRuns( labId, QString(), QString(), 0, 50 );
    if ( !runs )
        return thumbnails;

    const QStringList renderableSuffixes{
        QStringLiteral( "tif" ), QStringLiteral( "tiff" ), QStringLiteral( "png" ),
        QStringLiteral( "jpg" ), QStringLiteral( "jpeg" ),
    };
    for ( const auto &run : runs.value().second )
    {
        for ( const auto &artifact : run.artifacts() )
        {
            if ( thumbnails.size() >= kMaxReportThumbnails )
                return thumbnails;
            QFileInfo info( artifact.path );
            if ( !renderableSuffixes.contains( info.suffix().toLower() ) )
                continue;
            sicnu::experiment::LabReportThumbnail thumbnail;
            thumbnail.sourcePath = artifact.path;
            thumbnail.sourceSizeBytes = info.size();
            const auto preview =
                sicnu::app::renderRasterPreview( artifact.path, QSize( 512, 512 ) );
            if ( preview.status == sicnu::app::PreviewRender::Status::Ready )
            {
                QBuffer pngBuffer;
                if ( preview.image.save( &pngBuffer, "PNG" ) )
                    thumbnail.pngBytes = pngBuffer.buffer();
                else
                    thumbnail.error = QStringLiteral( "PNG encoding failed" );
            }
            else
            {
                thumbnail.error = preview.error;
            }
            thumbnails.append( thumbnail );
        }
    }
    return thumbnails;
}

} // namespace

void QgisDesktopWindow::newProject()
{
    if (!confirmWorkbenchShutdown(tr("新建工程")))
        return;
    if (!checkUnsavedChanges())
        return;

    if ( !m_projectContext )
    {
        QMessageBox::warning( this, tr( "New Project" ),
                              tr( "The project Data Context is unavailable." ) );
        return;
    }

    if ( m_mapCanvas )
        m_mapCanvas->stopRenderingAndSettle();

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

    // The cleared project has no lab: stop recording so later runs cannot
    // land in the previous project's experiment store.
    stopLabRecording();

    m_mapCanvas->setLayers({});
    m_mapCanvas->refresh();
    updateCanvasEmptyState();
    updateLayersEmptyState();
    updateEditingUI(nullptr);
    updateWindowTitle();
    refreshWorkspaceBrowser();
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
    if (!confirmWorkbenchShutdown(tr("打开工程")))
        return;
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

        if ( m_mapCanvas )
            m_mapCanvas->stopRenderingAndSettle();

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

        // Workspace Governance 3.0: the store must be open BEFORE the read so
        // the serializer can restore governed state from a v3 document (or run
        // the in-memory v1 migration into it).
        if ( !m_projectContext->openWorkspaceStore( filePath ) )
            statusBar()->showMessage( tr( "治理存储不可用：工作区状态将以仅内存模式运行" ), 5000 );

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
        updateWindowTitle();
        refreshWorkspaceBrowser();
        // D5: this project's lab executions now register as experiment runs
        // (opt-out via QSettings lab/autoRecordExperimentRuns).
        ensureLabRecordingForProject( filePath );
        statusBar()->showMessage(tr("已打开工程：%1").arg(filePath), 3000);
    }
}

void QgisDesktopWindow::saveProject()
{
    if (QgsProject::instance()->fileName().isEmpty()) {
        saveProjectAs();
    } else {
        QgsProject::instance()->write();
        updateWindowTitle();
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
        // Reopen the governance store against the new project file so governed
        // state follows Save As instead of bleeding across projects.
        if ( m_projectContext )
            m_projectContext->reopenWorkspaceStore( filePath );
        QgsProject::instance()->write(filePath);
        updateWindowTitle();
        refreshWorkspaceBrowser();
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
    auto &logger = sicnu::operators::RSOperationLogger::instance();
    sicnu::experiment::LabRunRecorder *recorder = s_labRecorder;
    sicnu::experiment::ExperimentStore *store =
        recorder && recorder->store() ? recorder->store() : nullptr;

    if ( logger.recordCount() == 0 && ( !store || recorder->recordedExecutionRefs().isEmpty() ) )
    {
        QMessageBox::information( this, tr( "Export Lab Report" ),
                                  tr( "No operations have been recorded yet." ) );
        return;
    }
    // The report anchors on registered experiment runs (lineage + replay
    // need a run identity). With auto-recording opted out there is no run to
    // anchor to — say so instead of exporting an unanchored trail.
    if ( !store || recorder->recordedExecutionRefs().isEmpty() )
    {
        QMessageBox::information(
            this, tr( "Export Lab Report" ),
            tr( "No experiment runs are registered for this project.\n\n"
                "Lab auto-recording is opt-out: it enables when a project is "
                "opened (setting: %1)." )
                .arg( QLatin1String( kLabAutoRecordKey ) ) );
        return;
    }

    // Header metadata: asked once per export, prefilled + persisted — the
    // report never invents a student or session identity.
    QSettings settings;
    bool ok = false;
    const QString student = QInputDialog::getText(
        this, tr( "Export Lab Report" ), tr( "学生姓名：" ), QLineEdit::Normal,
        settings.value( QLatin1String( kLabStudentKey ) ).toString(), &ok );
    if ( !ok )
        return;
    const QString session = QInputDialog::getText(
        this, tr( "Export Lab Report" ), tr( "实验 session（班级/批次）：" ), QLineEdit::Normal,
        settings.value( QLatin1String( kLabSessionKey ) ).toString(), &ok );
    if ( !ok )
        return;
    settings.setValue( QLatin1String( kLabStudentKey ), student );
    settings.setValue( QLatin1String( kLabSessionKey ), session );

    sicnu::experiment::LabReportRequest request;
    request.labId = recorder->experimentId();
    request.student = student;
    request.session = session;
    request.operationTrail =
        sicnu::experiment::jsonCppToQJson( logger.toJson() ).toArray();
    request.thumbnails = collectThumbnails( request.labId );
    // Grade seam: D4's LabGradeResult is not merged; the typed
    // "unavailable" status is the honest embedding (never a fake score).
    request.grade = sicnu::experiment::LabGradeEmbedding::unavailable();

    sicnu::experiment::LabReportBuilder builder( *store, nullptr );
    auto document = builder.build( request );
    if ( !document )
    {
        QMessageBox::warning( this, tr( "Export Lab Report" ),
                              tr( "Failed to build the lab report:\n%1" )
                                  .arg( document.diagnostics().first().message ) );
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr( "Export Lab Report" ), QStringLiteral( "lab-report" ),
        tr( "可打印 HTML 报告 (*.html);;JSON (*.json);;Markdown (*.md)" ) );
    if ( filePath.isEmpty() )
        return;

    // One base path, three format siblings (json/md/html) of the SAME schema.
    const QFileInfo targetInfo( filePath );
    const QString basePath =
        targetInfo.dir().filePath( targetInfo.completeBaseName() );
    auto written =
        sicnu::experiment::writeLabReportFiles( document.value(), basePath );
    if ( !written )
    {
        QMessageBox::warning( this, tr( "Export Lab Report" ),
                              tr( "Failed to export report:\n%1" )
                                  .arg( written.diagnostics().first().message ) );
        return;
    }

    statusBar()->showMessage(
        tr( "Lab report exported: %1" ).arg( written.value().join( QStringLiteral( ", " ) ) ),
        5000 );
}
