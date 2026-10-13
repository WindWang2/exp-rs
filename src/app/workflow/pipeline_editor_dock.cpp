// src/app/workflow/pipeline_editor_dock.cpp
#include "pipeline_editor_dock.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QDateTime>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "workflow_definition.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sicnu::workflow::gui {

PipelineEditorDock::PipelineEditorDock( QWidget *parent )
  : QDockWidget( tr( "Task Workflow Editor" ), parent )
{
  setObjectName( QStringLiteral( "rsPipelineEditorDock" ) );
  setAllowedAreas( Qt::AllDockWidgetAreas );

  auto *mainWidget = new QWidget( this );
  auto *layout = new QVBoxLayout( mainWidget );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->setSpacing( 0 );

  mSplitter = new QSplitter( Qt::Horizontal, mainWidget );
  mSplitter->setObjectName( QStringLiteral( "rsPipelineSplitter" ) );
  mCanvasWidget = new PipelineCanvasWidget( mSplitter );
  mPresetWidget = new PresetCatalogWidget( mSplitter );

  mSplitter->addWidget( mCanvasWidget );
  mSplitter->addWidget( mPresetWidget );
  mSplitter->setStretchFactor( 0, 4 );
  mSplitter->setStretchFactor( 1, 1 );

  createToolBar();
  layout->addWidget( mToolBar );
  layout->addWidget( mSplitter, 1 );
  setWidget( mainWidget );

  connect( mPresetWidget, &PresetCatalogWidget::presetSelected, this, &PipelineEditorDock::onPresetSelected );
}

void PipelineEditorDock::createToolBar()
{
  mToolBar = new QToolBar( this );
  mToolBar->setObjectName( QStringLiteral( "rsPipelineToolBar" ) );
  mToolBar->setIconSize( QSize( 16, 16 ) );

  auto *newAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "document-new" ), QIcon( QStringLiteral( ":/icons/document-new" ) ) ), tr( "New" ) );
  newAct->setToolTip( tr( "New Workflow" ) );
  connect( newAct, &QAction::triggered, this, &PipelineEditorDock::onNewClicked );

  auto *openAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "document-open" ), QIcon( QStringLiteral( ":/icons/document-open" ) ) ), tr( "Open" ) );
  openAct->setToolTip( tr( "Open Workflow (.json) — or via the command palette workflow.open" ) );
  connect( openAct, &QAction::triggered, this, &PipelineEditorDock::onOpenClicked );

  auto *saveAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "document-save" ), QIcon( QStringLiteral( ":/icons/document-save" ) ) ), tr( "Save" ) );
  saveAct->setToolTip( tr( "Save Workflow (.json)" ) );
  connect( saveAct, &QAction::triggered, this, &PipelineEditorDock::onSaveClicked );

  mToolBar->addSeparator();

  auto *runAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "media-playback-start" ), QIcon( QStringLiteral( ":/icons/media-playback-start" ) ) ), tr( "Run Full Pipeline" ) );
  runAct->setToolTip( tr( "Run Full Pipeline (scheduled in topological order)" ) );
  connect( runAct, &QAction::triggered, this, &PipelineEditorDock::onRunFullClicked );

  auto *stopAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "media-playback-stop" ), QIcon( QStringLiteral( ":/icons/media-playback-stop" ) ) ), tr( "Stop" ) );
  stopAct->setToolTip( tr( "Stop the running pipeline task" ) );
  connect( stopAct, &QAction::triggered, this, &PipelineEditorDock::onStopClicked );

  // Workbench 10.0: editor-side preflight projection. The SAME
  // workflow:preflight engine agents use validates the canvas DAG; issues
  // scoped to a step light that node's error badge, and every finding stays
  // listed with its repair suggestion.
  auto *preflightAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "to_ology_check" ), QIcon( QStringLiteral( ":/icons/to_ology_check" ) ) ), tr( "Check" ) );
  preflightAct->setToolTip( tr( "Check workflow structure (loops / unknown operators / missing parameters / output conflicts); errors map to node badges" ) );
  preflightAct->setObjectName( QStringLiteral( "rsPipelinePreflightAction" ) );
  connect( preflightAct, &QAction::triggered, this, &PipelineEditorDock::runPreflightProjection );

  mToolBar->addSeparator();

  auto *zoomFitAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "zoom-fit-best" ), QIcon( QStringLiteral( ":/icons/zoom-fit-best" ) ) ), tr( "Fit in Window" ) );
  zoomFitAct->setToolTip( tr( "Fit the window to show all nodes" ) );
  connect( zoomFitAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::zoomToFit );

  auto *zoomResetAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "zoom-original" ), QIcon( QStringLiteral( ":/icons/zoom-original" ) ) ), tr( "100% View" ) );
  zoomResetAct->setToolTip( tr( "Reset to 100% Scale" ) );
  connect( zoomResetAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::resetZoom );

  auto *deleteAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "edit-delete" ), QIcon( QStringLiteral( ":/icons/edit-delete" ) ) ), tr( "Delete Selected" ) );
  deleteAct->setToolTip( tr( "Delete selected nodes or links (Delete)" ) );
  connect( deleteAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::deleteSelected );

  mToolBar->addSeparator();

  auto *presetAct = mToolBar->addAction( QIcon::fromTheme( QStringLiteral( "bookmarks" ), QIcon( QStringLiteral( ":/icons/bookmarks" ) ) ), tr( "Preset Templates" ) );
  presetAct->setToolTip( tr( "Expand the preset template library" ) );
  connect( presetAct, &QAction::triggered, this, &PipelineEditorDock::onTogglePresetCatalog );
}

void PipelineEditorDock::onNewClicked()
{
  if ( mCanvasWidget && mCanvasWidget->pipelineScene() )
  {
    if ( !mCanvasWidget->pipelineScene()->nodes().empty() )
    {
      auto reply = QMessageBox::question( this,
                                          tr( "Confirm New Pipeline" ),
                                          tr( "Creating a new pipeline will clear the current canvas. Continue?" ),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No );
      if ( reply != QMessageBox::Yes )
        return;
    }
    mCanvasWidget->pipelineScene()->clearWorkflow();
  }
  emit newWorkflowRequested();
}

bool PipelineEditorDock::writePipelineDefinitionFile( const QString &filePath,
                                                     const Json::Value &root,
                                                     QString *errorOut )
{
  if ( filePath.isEmpty() )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Target file path is empty." );
    return false;
  }

  QFileInfo fileInfo( filePath );
  QDir parentDir = fileInfo.dir();
  if ( !parentDir.exists() )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Directory does not exist: %1" ).arg( parentDir.absolutePath() );
    return false;
  }

  QTemporaryFile tempFile( parentDir.filePath( QStringLiteral( "pipeline_save_XXXXXX.tmp" ) ) );
  tempFile.setAutoRemove( true );
  if ( !tempFile.open() )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Cannot create temporary file: %1" ).arg( tempFile.errorString() );
    return false;
  }

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, root );

  qint64 written = tempFile.write( text.data(), text.size() );
  if ( written != static_cast<qint64>( text.size() ) || !tempFile.flush() )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Failed to write complete data: %1" ).arg( tempFile.errorString() );
    return false;
  }

  const QString tempName = tempFile.fileName();
  tempFile.close();

  tempFile.setAutoRemove( false );
#if defined(Q_OS_UNIX)
  if ( ::rename( QFile::encodeName( tempName ).constData(),
                 QFile::encodeName( filePath ).constData() ) != 0 )
  {
    tempFile.setAutoRemove( true );
    if ( errorOut )
      *errorOut = QObject::tr( "Failed to atomically publish pipeline file: %1" )
                      .arg( QString::fromLocal8Bit( strerror( errno ) ) );
    return false;
  }
#else
  if ( QFile::exists( filePath ) )
  {
    const QString backupPath = filePath + QStringLiteral( ".bak.%1" ).arg( QDateTime::currentMSecsSinceEpoch() );
    if ( !QFile::rename( filePath, backupPath ) )
    {
      tempFile.setAutoRemove( true );
      if ( errorOut )
        *errorOut = QObject::tr( "Cannot backup existing file before replace: %1" ).arg( filePath );
      return false;
    }
    if ( !QFile::rename( tempName, filePath ) )
    {
      QFile::rename( backupPath, filePath );
      tempFile.setAutoRemove( true );
      if ( errorOut )
        *errorOut = QObject::tr( "Failed to publish pipeline file: %1" ).arg( filePath );
      return false;
    }
    QFile::remove( backupPath );
  }
  else
  {
    if ( !QFile::rename( tempName, filePath ) )
    {
      tempFile.setAutoRemove( true );
      if ( errorOut )
        *errorOut = QObject::tr( "Failed to publish pipeline file: %1" ).arg( filePath );
      return false;
    }
  }
#endif

  return true;
}

bool PipelineEditorDock::readPipelineDefinitionFile( const QString &filePath,
                                                    Json::Value &rootOut,
                                                    QString *errorOut )
{
  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Cannot open file: %1 (%2)" ).arg( filePath, file.errorString() );
    return false;
  }

  const QByteArray data = file.readAll();
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  std::string errs;
  std::istringstream stream( std::string( data.constData(), static_cast<size_t>( data.size() ) ) );
  if ( !Json::parseFromStream( builder, stream, &rootOut, &errs ) )
  {
    if ( errorOut )
      *errorOut = QString::fromStdString( errs );
    return false;
  }
  return true;
}

void PipelineEditorDock::onOpenClicked()
{
  QString fileName = QFileDialog::getOpenFileName( this, tr( "Open Pipeline Definition JSON" ), QString(), tr( "JSON Files (*.json)" ) );
  if ( fileName.isEmpty() )
    return;

  Json::Value root;
  QString err;
  if ( !readPipelineDefinitionFile( fileName, root, &err ) )
  {
    QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open file: %1" ).arg( err ) );
    return;
  }

  WorkflowDefinition def;
  std::string parseErr;
  if ( !workflowDefinitionFromJson( root, def, parseErr ) )
  {
    QMessageBox::warning( this, tr( "Format Error" ), QString::fromStdString( parseErr ) );
    return;
  }

  mCanvasWidget->loadWorkflowDefinition( def );
  emit openWorkflowRequested();
}

void PipelineEditorDock::onSaveClicked()
{
  QString fileName = QFileDialog::getSaveFileName( this, tr( "Save Pipeline Definition JSON" ), QStringLiteral( "workflow.json" ), tr( "JSON Files (*.json)" ) );
  if ( fileName.isEmpty() )
    return;

  WorkflowDefinition baseDef;
  baseDef.id = "exported_pipeline";
  baseDef.title = "Exported Pipeline";

  WorkflowDefinition exported = mCanvasWidget->exportWorkflowDefinition( baseDef );
  Json::Value root = workflowDefinitionToJson( exported );

  QString err;
  if ( !writePipelineDefinitionFile( fileName, root, &err ) )
  {
    QMessageBox::warning( this, tr( "Save Failed" ), tr( "Cannot write pipeline definition:\n%1" ).arg( err ) );
    return;
  }

  QMessageBox::information( this, tr( "Succeeded" ), tr( "Pipeline definition saved to: %1" ).arg( fileName ) );
  emit saveWorkflowRequested();
}

void PipelineEditorDock::onRunFullClicked()
{
  emit runFullWorkflowRequested();
}

void PipelineEditorDock::onStopClicked()
{
  emit stopWorkflowRequested();
}

void PipelineEditorDock::runPreflightProjection()
{
  if ( !mCanvasWidget || !mCanvasWidget->pipelineScene() )
    return;

  WorkflowDefinition baseDef;
  baseDef.id = "editor_preflight";
  baseDef.title = "Editor Pipeline";
  const WorkflowDefinition exported = mCanvasWidget->exportWorkflowDefinition( baseDef );
  const Json::Value workflowJson = workflowDefinitionToJson( exported );

  Json::Value input( Json::objectValue );
  input["workflow"] = workflowJson;
  auto tool = sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find(
      "workflow:preflight" );
  if ( !tool )
  {
    QMessageBox::warning( this, tr( "Check" ), tr( "The workflow:preflight tool is not registered." ) );
    return;
  }
  const auto result = ( *tool )->execute( input );
  if ( !result.success )
  {
    QMessageBox::warning( this, tr( "Check" ),
                          QString::fromStdString( result.error ) );
    return;
  }

  // Reset every node to Idle, then project error-severity issues onto the
  // step-scoped nodes.
  auto *scene = mCanvasWidget->pipelineScene();
  for ( auto &[id, node] : scene->nodes() )
  {
    Q_UNUSED( id );
    if ( node && node->status() == NodeStatus::Failure )
      node->setStatus( NodeStatus::Idle );
  }

  QStringList lines;
  int errors = 0;
  const Json::Value issues = result.output.isMember( "issues" ) && result.output["issues"].isArray()
                                 ? result.output["issues"]
                                 : Json::Value( Json::arrayValue );
  for ( const auto &issue : issues )
  {
    const std::string message = issue.isMember( "message" ) ? issue["message"].asString() : "";
    const std::string severity = issue.isMember( "severity" ) ? issue["severity"].asString() : "";
    const std::string stepId = issue.isMember( "item_id" ) ? issue["item_id"].asString() : "";
    if ( severity == "error" )
    {
      ++errors;
      if ( auto *node = scene->findNode( QString::fromStdString( stepId ) ) )
        node->setStatus( NodeStatus::Failure );
    }
    lines << QStringLiteral( "[%1] %2%3" )
                 .arg( QString::fromStdString( severity ).toUpper(),
                       QString::fromStdString( message ),
                       issue.isMember( "repairable" ) && issue["repairable"].asBool()
                           ? tr( "(fixable)" )
                           : QString() );
  }

  const QString verdict = result.output.isMember( "verdict" ) && result.output["verdict"].isString()
                              ? QString::fromStdString( result.output["verdict"].asString() )
                              : QString();
  if ( errors == 0 )
    QMessageBox::information( this, tr( "Check" ),
                              tr( "No structural issues found (verdict: %1)." ).arg( verdict ) );
  else
    QMessageBox::warning( this, tr( "Check found %1 issue(s)" ).arg( errors ), lines.join( QLatin1Char( '\n' ) ) );
}

void PipelineEditorDock::onTogglePresetCatalog()
{
  if ( mPresetWidget )
  {
    mPresetWidget->setVisible( !mPresetWidget->isVisible() );
  }
}

void PipelineEditorDock::onPresetSelected( const sicnu::workflow::WorkflowDefinition &def )
{
  if ( mCanvasWidget && mCanvasWidget->pipelineScene() )
  {
    if ( !mCanvasWidget->pipelineScene()->nodes().empty() )
    {
      auto reply = QMessageBox::question( this,
                                          tr( "Confirm Template Loading" ),
                                          tr( "Loading a preset template will replace the pipeline on the canvas. Continue?" ),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No );
      if ( reply != QMessageBox::Yes )
        return;
    }
    mCanvasWidget->loadWorkflowDefinition( def );
  }
}

} // namespace sicnu::workflow::gui
