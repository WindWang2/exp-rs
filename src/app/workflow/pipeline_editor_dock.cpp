// src/app/workflow/pipeline_editor_dock.cpp
#include "pipeline_editor_dock.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <sstream>

#include "workflow_definition.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "durable_file_write.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUndoCommand>
#include <QUndoStack>

namespace sicnu::workflow::gui {
namespace
{

class WorkflowUndoCommand final : public QUndoCommand
{
  public:
    WorkflowUndoCommand( PipelineCanvasWidget *canvas,
                         const WorkflowDefinition &before,
                         const WorkflowDefinition &after,
                         bool *loadingGuard )
      : m_canvas( canvas ), m_before( before ), m_after( after ), m_loadingGuard( loadingGuard )
    {
      setText( QObject::tr( "Edit Workflow" ) );
    }

    void undo() override { apply( m_before ); }
    void redo() override
    {
      if ( m_applyOnRedo )
        apply( m_after );
      m_applyOnRedo = true;
    }

  private:
    void apply( const WorkflowDefinition &definition )
    {
      if ( !m_canvas )
        return;
      if ( m_loadingGuard )
        *m_loadingGuard = true;
      m_canvas->loadWorkflowDefinition( definition );
      if ( m_loadingGuard )
        *m_loadingGuard = false;
    }

    QPointer<PipelineCanvasWidget> m_canvas;
    WorkflowDefinition m_before;
    WorkflowDefinition m_after;
    bool *m_loadingGuard = nullptr;
    bool m_applyOnRedo = false;
};

} // namespace

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

  m_undoStack = new QUndoStack( this );
  m_currentDefinition = currentCanvasDefinition();
  createToolBar();
  layout->addWidget( mToolBar );
  layout->addWidget( mSplitter, 1 );
  setWidget( mainWidget );

  connect( mPresetWidget, &PresetCatalogWidget::presetSelected, this, &PipelineEditorDock::onPresetSelected );
  connect( mCanvasWidget, &PipelineCanvasWidget::workflowChanged,
           this, &PipelineEditorDock::recordWorkflowChange );
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

  QAction *undoAct = m_undoStack->createUndoAction( this, tr( "Undo" ) );
  undoAct->setObjectName( QStringLiteral( "rsPipelineUndoAction" ) );
  undoAct->setIcon( QIcon::fromTheme( QStringLiteral( "edit-undo" ), QIcon( QStringLiteral( ":/icons/undo" ) ) ) );
  mToolBar->addAction( undoAct );
  QAction *redoAct = m_undoStack->createRedoAction( this, tr( "Redo" ) );
  redoAct->setObjectName( QStringLiteral( "rsPipelineRedoAction" ) );
  redoAct->setIcon( QIcon::fromTheme( QStringLiteral( "edit-redo" ), QIcon( QStringLiteral( ":/icons/redo" ) ) ) );
  mToolBar->addAction( redoAct );

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

sicnu::workflow::WorkflowDefinition PipelineEditorDock::currentCanvasDefinition() const
{
  WorkflowDefinition base = m_currentDefinition;
  if ( base.id.empty() )
    base.id = "editor_history";
  if ( base.title.empty() )
    base.title = tr( "Workflow" ).toStdString();
  return mCanvasWidget->exportWorkflowDefinition( base );
}

void PipelineEditorDock::recordWorkflowChange()
{
  const WorkflowDefinition updated = currentCanvasDefinition();
  if ( workflowDefinitionToJson( m_currentDefinition ) == workflowDefinitionToJson( updated ) )
    return;
  if ( !m_undoInProgress && m_undoStack )
    m_undoStack->push( new WorkflowUndoCommand( mCanvasWidget, m_currentDefinition, updated,
                                                &m_undoInProgress ) );
  m_currentDefinition = updated;
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
  // R14-D WP-B (#1487 remainder): the hand-rolled temp/fsync/rename/dir-fsync
  // lane (and the Windows backup-then-rename dance with its target-absent
  // window) is replaced by the shared crash-or-valid authority. Error strings
  // for the tested paths are preserved verbatim ("Target file path is empty.",
  // "Target is a directory: …", "Directory does not exist: …", "Cannot create
  // temporary file: …" — see tests/test_workflow_pipeline_ui.cpp:698-863),
  // and the lane GAINS: destination-permission preservation across the
  // publish (T-d), symlink-referent resolution (T-e) and a single-syscall
  // publish on Windows (T-a). fsync + dir-fsync semantics are unchanged.
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, root );

  const sicnu::DurableWriteResult result = sicnu::writeBytesDurably(
    filePath, QByteArray( text.data(), static_cast<qsizetype>( text.size() ) ) );
  if ( !result.ok )
  {
    if ( errorOut )
      *errorOut = result.error;
    return false;
  }
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
  // #1793 (#1607 discipline): a user-opened document — pin the nesting
  // bound and catch the reader's throw: jsoncpp THROWS past the nesting
  // limit, and an uncaught throw inside a Qt slot aborts the process.
  builder["stackLimit"] = 256;
  std::string errs;
  std::istringstream stream( std::string( data.constData(), static_cast<size_t>( data.size() ) ) );
  try
  {
    if ( !Json::parseFromStream( builder, stream, &rootOut, &errs ) )
    {
      if ( errorOut )
        *errorOut = QString::fromStdString( errs );
      return false;
    }
  }
  catch ( const std::exception &e )
  {
    if ( errorOut )
      *errorOut = QObject::tr( "Refused to parse %1: %2" )
                    .arg( filePath, QString::fromUtf8( e.what() ) );
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
  const Json::Value root = workflowDefinitionToJson( exported );

  QString err;
  if ( !writePipelineDefinitionFile( fileName, root, &err ) )
  {
    QMessageBox::warning( this, tr( "Save Failed" ), tr( "Cannot write pipeline definition:\n%1" ).arg( err ) );
    return;
  }

  QMessageBox::information( this, tr( "Succeeded" ), tr( "Pipeline definition saved to: %1" ).arg( fileName ) );
  emit saveWorkflowRequested();
}

bool writePipelineDefinitionFile( const QString &path,
                                  const sicnu::workflow::WorkflowDefinition &def,
                                  QString *error )
{
  const Json::Value root = workflowDefinitionToJson( def );
  return PipelineEditorDock::writePipelineDefinitionFile( path, root, error );
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
