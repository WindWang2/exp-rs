/***************************************************************************
 * cartography_dock.cpp — desktop surface over the cartography operator family
 *
 * Production 11.0: every action runs as a TaskCenter job (the SAME
 * registry dispatch a workflow node takes) — no operator executes on the
 * GUI thread anymore. The dock keeps its no-engine rule: it drafts specs,
 * submits cartography:* operators, and renders their structured reports.
 ***************************************************************************/
#include "cartography_dock.h"

#include "agent/cartography/registry.h"
#include "operators/framework/rs_operator_registry.h"

#include "../shell/rs_job_runner.h"

#include <processing/framework/task_center.h>

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <qgslayoutexporter.h>
#include <qgsprintlayout.h>

#include "../agent/layout_tools/layout_service.h"

namespace sicnu::app
{

namespace
{

using sicnu::agent::layout_tools::LayoutService;

/// Bounded preview budget: the label shows a page-shaped projection, the
/// export stays the evidence path.
constexpr int kMaxPreviewWidthPx = 1100;
constexpr double kPreviewDpi = 72.0;

QString prettyJson( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return QString::fromStdString( Json::writeString( builder, value ) );
}

} // namespace

CartographyDock::CartographyDock( LayerSourcesProvider layers, WorkDirProvider workDir,
                                  QWidget *parent )
    : QgsDockWidget( parent )
    , m_layers( std::move( layers ) )
    , m_workDir( std::move( workDir ) )
{
    setObjectName( QStringLiteral( "rsCartographyDock" ) );
    setWindowTitle( tr( "Cartography Workbench" ) );
    buildUi();
    reloadTemplates();
}

void CartographyDock::buildUi()
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );
    layout->setContentsMargins( 8, 8, 8, 8 );

    // ── Draft inputs ────────────────────────────────────────────────────
    auto *templateRow = new QHBoxLayout;
    m_templateCombo = new QComboBox( central );
    m_templateCombo->setObjectName( QStringLiteral( "rsCartographyTemplate" ) );
    m_templateCombo->setAccessibleName( tr( "Map template" ) );
    m_templateCombo->setToolTip( tr( "Templates come from the cartography component catalog (same source as the agent tools)." ) );
    templateRow->addWidget( new QLabel( tr( "Template:" ), central ), 0 );
    templateRow->addWidget( m_templateCombo, 1 );
    layout->addLayout( templateRow );

    m_layoutNameEdit = new QLineEdit( QStringLiteral( "wb-map" ), central );
    m_layoutNameEdit->setObjectName( QStringLiteral( "rsCartographyLayoutName" ) );
    m_layoutNameEdit->setAccessibleName( tr( "Layout name" ) );
    layout->addWidget( m_layoutNameEdit );

    m_titleEdit = new QLineEdit( central );
    m_titleEdit->setObjectName( QStringLiteral( "rsCartographyTitle" ) );
    m_titleEdit->setAccessibleName( tr( "Map title" ) );
    m_titleEdit->setPlaceholderText( tr( "Map title" ) );
    layout->addWidget( m_titleEdit );

    m_sourceNoteEdit = new QLineEdit( central );
    m_sourceNoteEdit->setObjectName( QStringLiteral( "rsCartographySourceNote" ) );
    m_sourceNoteEdit->setAccessibleName( tr( "Data source note" ) );
    m_sourceNoteEdit->setPlaceholderText( tr( "Data source note (optional)" ) );
    layout->addWidget( m_sourceNoteEdit );

    // ── Actions (each one an operator dispatch) ─────────────────────────
    auto *composeRow = new QHBoxLayout;
    m_composeBtn = new QPushButton( tr( "Compose" ), central );
    m_composeBtn->setObjectName( QStringLiteral( "rsCartographyCompose" ) );
    m_composeBtn->setAccessibleName( tr( "Compose" ) );
    m_preflightBtn = new QPushButton( tr( "Check" ), central );
    m_preflightBtn->setObjectName( QStringLiteral( "rsCartographyPreflight" ) );
    m_preflightBtn->setAccessibleName( tr( "Check" ) );
    m_repairBtn = new QPushButton( tr( "Fix" ), central );
    m_repairBtn->setObjectName( QStringLiteral( "rsCartographyRepair" ) );
    m_repairBtn->setAccessibleName( tr( "Fix" ) );
    composeRow->addWidget( m_composeBtn );
    composeRow->addWidget( m_preflightBtn );
    composeRow->addWidget( m_repairBtn );
    composeRow->addStretch( 1 );
    layout->addLayout( composeRow );

    // ── Export row ──────────────────────────────────────────────────────
    auto *exportRow = new QHBoxLayout;
    m_formatCombo = new QComboBox( central );
    m_formatCombo->setObjectName( QStringLiteral( "rsCartographyFormat" ) );
    m_formatCombo->setAccessibleName( tr( "Export format" ) );
    m_formatCombo->addItems( { tr( "png" ), tr( "pdf" ), tr( "svg" ) } );
    m_dpiSpin = new QSpinBox( central );
    m_dpiSpin->setObjectName( QStringLiteral( "rsCartographyDpi" ) );
    m_dpiSpin->setAccessibleName( tr( "Export DPI" ) );
    m_dpiSpin->setRange( 72, 1200 );
    m_dpiSpin->setValue( 300 );
    m_directoryEdit = new QLineEdit( central );
    m_directoryEdit->setObjectName( QStringLiteral( "rsCartographyDirectory" ) );
    m_directoryEdit->setAccessibleName( tr( "Export directory" ) );
    m_directoryEdit->setPlaceholderText( tr( "Export directory" ) );
    auto *browseBtn = new QPushButton( tr( "…choose directory" ), central );
    browseBtn->setAccessibleName( tr( "Choose export directory" ) );
    m_exportBtn = new QPushButton( tr( "Export" ), central );
    m_exportBtn->setObjectName( QStringLiteral( "rsCartographyExport" ) );
    m_exportBtn->setAccessibleName( tr( "Export" ) );
    m_produceBtn = new QPushButton( tr( "Production export" ), central );
    m_produceBtn->setObjectName( QStringLiteral( "rsCartographyProduce" ) );
    m_produceBtn->setAccessibleName( tr( "Production export (compose → fix → export → manifest)" ) );
    m_produceBtn->setToolTip(
      tr( "One pass: compose → bounded fix → export (with atlas) → manifest sidecar; atomic publish." ) );
    m_stopBtn = new QPushButton( tr( "Stop" ), central );
    m_stopBtn->setObjectName( QStringLiteral( "rsCartographyStop" ) );
    m_stopBtn->setAccessibleName( tr( "Stop the current cartography task" ) );
    m_stopBtn->setEnabled( false );
    exportRow->addWidget( m_formatCombo );
    exportRow->addWidget( new QLabel( tr( "DPI：" ), central ) );
    exportRow->addWidget( m_dpiSpin );
    exportRow->addWidget( m_directoryEdit, 1 );
    exportRow->addWidget( browseBtn );
    exportRow->addWidget( m_exportBtn );
    exportRow->addWidget( m_produceBtn );
    exportRow->addWidget( m_stopBtn );
    layout->addLayout( exportRow );

    connect( browseBtn, &QPushButton::clicked, this, [this] {
        const QString start = m_directoryEdit->text().isEmpty()
                                  ? ( m_workDir ? m_workDir() : QString() )
                                  : m_directoryEdit->text();
        const QString dir = QFileDialog::getExistingDirectory( this, tr( "Export directory" ), start );
        if ( !dir.isEmpty() )
            m_directoryEdit->setText( dir );
    } );

    // ── Preview + report ────────────────────────────────────────────────
    m_previewLabel = new QLabel( central );
    m_previewLabel->setObjectName( QStringLiteral( "rsCartographyPreview" ) );
    m_previewLabel->setAccessibleName( tr( "Compose preview" ) );
    m_previewLabel->setMinimumSize( 320, 220 );
    m_previewLabel->setAlignment( Qt::AlignCenter );
    m_previewLabel->setText( tr( "No preview yet — pick a template and click Compose." ) );
    auto *scroll = new QScrollArea( central );
    scroll->setWidgetResizable( true );
    scroll->setWidget( m_previewLabel );
    layout->addWidget( scroll, 2 );

    m_reportView = new QPlainTextEdit( central );
    m_reportView->setObjectName( QStringLiteral( "rsCartographyReport" ) );
    m_reportView->setAccessibleName( tr( "Quality report" ) );
    m_reportView->setReadOnly( true );
    m_reportView->setPlaceholderText( tr( "Check / fix / export reports appear here." ) );
    layout->addWidget( m_reportView, 1 );

    connect( m_composeBtn, &QPushButton::clicked, this, &CartographyDock::runCompose );
    connect( m_preflightBtn, &QPushButton::clicked, this, &CartographyDock::runPreflight );
    connect( m_repairBtn, &QPushButton::clicked, this, &CartographyDock::runRepair );
    connect( m_exportBtn, &QPushButton::clicked, this, &CartographyDock::runExport );
    connect( m_produceBtn, &QPushButton::clicked, this, &CartographyDock::runProduce );
    connect( m_stopBtn, &QPushButton::clicked, this, &CartographyDock::cancelRunningJob );
    setWidget( central );
}

void CartographyDock::reloadTemplates()
{
    const Json::Value templates = sicnu::agent::cartography::TemplateRegistry::instance().templates();
    m_templateCombo->clear();
    for ( const auto &entry : templates )
    {
        if ( entry.isObject() && entry.isMember( "id" ) )
        {
            const QString id = QString::fromStdString( entry["id"].asString() );
            const QString description =
                entry.isMember( "description" ) && entry["description"].isString()
                    ? QString::fromStdString( entry["description"].asString() )
                    : QString();
            m_templateCombo->addItem( id, id );
            if ( !description.isEmpty() )
                m_templateCombo->setItemData( m_templateCombo->count() - 1, description,
                                              Qt::ToolTipRole );
        }
    }
}

QStringList CartographyDock::templateIds() const
{
    QStringList ids;
    for ( int i = 0; i < m_templateCombo->count(); ++i )
        ids.append( m_templateCombo->itemText( i ) );
    return ids;
}

QString CartographyDock::lastReportText() const
{
    return m_reportView ? m_reportView->toPlainText() : QString();
}

Json::Value CartographyDock::buildDraft( QString *error ) const
{
    const QString templateId = m_templateCombo->currentData().toString();
    if ( templateId.isEmpty() )
    {
        if ( error )
            *error = tr( "No templates available: check the data/cartography/templates directory." );
        return {};
    }
    const QStringList sources = m_layers ? m_layers() : QStringList();
    if ( sources.isEmpty() )
    {
        if ( error )
            *error = tr( "No map layers available: add data to the canvas before composing." );
        return {};
    }
    Json::Value params( Json::objectValue );
    params["layout_name"] = m_layoutNameEdit->text().trimmed().toStdString();
    if ( !m_titleEdit->text().trimmed().isEmpty() )
        params["title"] = m_titleEdit->text().trimmed().toStdString();
    if ( !m_sourceNoteEdit->text().trimmed().isEmpty() )
        params["source_note"] = m_sourceNoteEdit->text().trimmed().toStdString();
    Json::Value layers( Json::arrayValue );
    for ( const QString &source : sources )
        layers.append( source.toStdString() );
    params["layers"] = layers;

    QString instantiateError;
    const Json::Value draft = sicnu::agent::cartography::TemplateRegistry::instance()
                                  .instantiateTemplate( templateId, params, &instantiateError );
    if ( draft.isNull() )
    {
        if ( error )
            *error = tr( "Template instantiation failed: %1" ).arg( instantiateError );
        return {};
    }
    return draft;
}

bool CartographyDock::submitOperatorJob(
  const QString &operatorId, const Json::Value &params,
  const std::function<void( const Json::Value &, const QString & )> &onDone )
{
    if ( m_runningTaskId >= 0 )
    {
        emit statusMessage( tr( "A cartography task is already running (click Stop to cancel)." ) );
        return false;
    }
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId.toStdString() );
    if ( !op )
    {
        emit statusMessage(
          tr( "Operator not registered: %1 (the cartography operator family should register at startup)" ).arg( operatorId ) );
        return false;
    }

    // Registry dispatch: the JobEngine resolves algorithmId through
    // RSOperatorRegistry and runs op->run(params, ctx) on a worker with
    // TaskCenter-owned cancel/progress wiring — the identical path a
    // workflow node takes. The dock adds no execution semantics.
    sicnu::jobs::JobRequest request;
    request.algorithmId = operatorId.toStdString();
    request.title = operatorId.toStdString();
    request.source = "ui";
    request.params = params;

    const long taskId = sicnu::TaskCenter::instance().submitJob( request );
    if ( taskId <= 0 )
    {
        emit statusMessage( tr( "Failed to submit %1 to the TaskCenter." ).arg( operatorId ) );
        return false;
    }
    m_runningTaskId = taskId;
    setBusy( true );

    auto *lifetime = new QObject( this );
    // Progress reflection: TaskCenter owns the authoritative progress feed
    // (its task panel renders it); the dock mirrors a compact status line.
    // Both connections ride the per-job lifetime object so stale handlers
    // disappear with the job.
    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, lifetime,
             [ this ]( const sicnu::AlgorithmTaskInfo &info ) {
                 if ( info.taskId != m_runningTaskId )
                     return;
                 if ( info.status == sicnu::TaskStatus::Running &&
                      info.progressPercentage >= 0.0 )
                     emit statusMessage( tr( "Cartography task in progress: %1%" )
                                           .arg( static_cast<int>( info.progressPercentage *
                                                                  100.0 ) ) );
             } );

    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, lifetime,
             [ this, lifetime, operatorId, onDone ]( const sicnu::AlgorithmTaskInfo &info ) {
                 if ( info.taskId != m_runningTaskId )
                     return;
                 if ( info.status != sicnu::TaskStatus::Completed &&
                      info.status != sicnu::TaskStatus::Failed &&
                      info.status != sicnu::TaskStatus::Canceled )
                     return;
                 m_runningTaskId = -1;
                 setBusy( false );
                 lifetime->deleteLater();
                 if ( info.status == sicnu::TaskStatus::Completed )
                 {
                     onDone( info.resultPayload, QString() );
                     return;
                 }
                 const QString message = info.errorMessage.isEmpty()
                                           ? tr( "%1 task terminated abnormally." ).arg( operatorId )
                                           : info.errorMessage;
                 if ( !info.resultPayload.isNull() && m_reportView )
                     showReport( tr( "%1 structured errors" ).arg( operatorId ), info.resultPayload );
                 onDone( {}, message );
             } );
    emit statusMessage( tr( "%1 submitted as background task (#%2)." ).arg( operatorId ).arg( taskId ) );
    return true;
}

void CartographyDock::setBusy( bool busy )
{
    m_composeBtn->setEnabled( !busy );
    m_preflightBtn->setEnabled( !busy );
    m_repairBtn->setEnabled( !busy );
    m_exportBtn->setEnabled( !busy );
    m_produceBtn->setEnabled( !busy );
    m_stopBtn->setEnabled( busy );
    m_templateCombo->setEnabled( !busy );
}

void CartographyDock::showReport( const QString &heading, const Json::Value &payload )
{
    if ( m_reportView )
        m_reportView->setPlainText( heading + "\n" + prettyJson( payload ) );
}

void CartographyDock::adoptSpec( const Json::Value &spec )
{
    m_currentSpec = spec;
    if ( spec.isObject() && spec.isMember( "layout_name" ) && spec["layout_name"].isString() )
        m_composedLayoutName = QString::fromStdString( spec["layout_name"].asString() );
}

void CartographyDock::runCompose()
{
    QString error;
    const Json::Value draft = buildDraft( &error );
    if ( !error.isEmpty() )
    {
        emit statusMessage( error );
        if ( m_reportView )
            m_reportView->setPlainText( error );
        return;
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = draft;
    const bool submitted = submitOperatorJob(
      QStringLiteral( "cartography:compose" ), params,
      [ this, draft ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          adoptSpec( result.isMember( "mapspec" ) ? result["mapspec"] : draft );
          const bool compiled = result.isMember( "compiled" ) && result["compiled"].asBool();
          showReport( compiled ? tr( "Compose finished (structured summary)" ) : tr( "Compose did not pass" ), result );
          if ( compiled )
          {
              updatePreview( m_composedLayoutName );
              emit statusMessage( tr( "Compose finished: layout %1 generated." ).arg( m_composedLayoutName ) );
          }
          else
          {
              emit statusMessage( tr( "Compose failed: see the quality report." ) );
          }
      } );
    if ( submitted )
        m_reportView->setPlainText( tr( "Compose task submitted." ) );
}

void CartographyDock::runPreflight()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    submitOperatorJob(
      QStringLiteral( "cartography:preflight" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          showReport( tr( "Check report" ), result );
          emit statusMessage( tr( "Check finished: see the quality report." ) );
      } );
}

void CartographyDock::runRepair()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    submitOperatorJob(
      QStringLiteral( "cartography:repair" ), params,
      [ this, spec ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          adoptSpec( result.isMember( "mapspec" ) ? result["mapspec"] : spec );
          showReport( tr( "Fix ledger (applied / still_reported)" ), result );
          emit statusMessage( tr( "Fix finished: %1 applied (%2 rounds)." )
                                  .arg( result.isMember( "repairs_applied" )
                                            ? result["repairs_applied"].asInt()
                                            : 0 )
                                  .arg( result.isMember( "iterations" )
                                            ? result["iterations"].asInt()
                                            : 0 ) );
      } );
}

void CartographyDock::runExport()
{
    if ( m_composedLayoutName.isEmpty() )
    {
        emit statusMessage( tr( "No composed layout yet: run Compose first." ) );
        return;
    }
    const QString directory =
        m_directoryEdit->text().trimmed().isEmpty() && m_workDir ? m_workDir()
                                                                 : m_directoryEdit->text().trimmed();
    if ( directory.isEmpty() )
    {
        emit statusMessage( tr( "Choose an export directory first." ) );
        return;
    }
    Json::Value params( Json::objectValue );
    params["layout"] = m_composedLayoutName.toStdString();
    params["format"] = m_formatCombo->currentText().toStdString();
    params["directory"] = directory.toStdString();
    params["dpi"] = m_dpiSpin->value();
    submitOperatorJob(
      QStringLiteral( "cartography:export" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          showReport( tr( "Export evidence (atomic write + sha256)" ), result );
          if ( !result.isObject() || !result.isMember( "path" ) || !result["path"].isString() )
          {
              emit statusMessage( tr( "Export result is missing a path (see report)." ) );
              return;
          }
          emit statusMessage( tr( "Export finished: %1" )
                                  .arg( QString::fromStdString( result["path"].asString() ) ) );
      } );
}

void CartographyDock::runProduce()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    const QString directory =
        m_directoryEdit->text().trimmed().isEmpty() && m_workDir ? m_workDir()
                                                                 : m_directoryEdit->text().trimmed();
    if ( directory.isEmpty() )
    {
        emit statusMessage( tr( "Choose an export directory first." ) );
        return;
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    params["directory"] = directory.toStdString();
    params["format"] = m_formatCombo->currentText().toStdString();
    params["dpi"] = m_dpiSpin->value();
    params["write_manifest"] = true;
    submitOperatorJob(
      QStringLiteral( "cartography:produce" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          if ( result.isMember( "mapspec" ) )
              adoptSpec( result["mapspec"] );
          showReport( tr( "Production delivery (atomic publish + manifest)" ), result );
          const QString delivered =
            result.isMember( "output" ) && result["output"].isString()
              ? QString::fromStdString( result["output"].asString() )
              : QString();
          const int pages = result.isMember( "page_count" ) ? result["page_count"].asInt() : 0;
          emit statusMessage( tr( "Production finished: %1 (%2 pages)" ).arg( delivered ).arg( pages ) );
      } );
}

void CartographyDock::cancelRunningJob()
{
    if ( m_runningTaskId < 0 )
        return;
    const long taskId = m_runningTaskId;
    if ( sicnu::TaskCenter::instance().cancelTask( taskId ) )
        emit statusMessage( tr( "Cancellation requested for task #%1." ).arg( taskId ) );
    else
        emit statusMessage( tr( "Task #%1 cannot be cancelled (it may have finished)." ).arg( taskId ) );
}

void CartographyDock::updatePreview( const QString &layoutName )
{
    QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
    if ( !layout )
    {
        m_previewLabel->setText( tr( "Layout %1 is unavailable (it may have been removed)." ).arg( layoutName ) );
        return;
    }
    QgsLayoutExporter exporter( layout );
    const QImage image = exporter.renderPageToImage( 0, QSize(), kPreviewDpi );
    if ( image.isNull() )
    {
        m_previewLabel->setText( tr( "Preview rendering failed (the layout can still be exported)." ) );
        return;
    }
    QPixmap pixmap = QPixmap::fromImage( image );
    if ( pixmap.width() > kMaxPreviewWidthPx )
        pixmap = pixmap.scaledToWidth( kMaxPreviewWidthPx, Qt::SmoothTransformation );
    m_previewLabel->setPixmap( pixmap );
}

} // namespace sicnu::app
