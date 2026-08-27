// sicnu_algorithm_dialog.cpp — Phase: Processing Toolbox overhaul
#include "sicnu_algorithm_dialog.h"
#include "dialog_help_catalog.h"
#include "main_window.h"
#include "shell/processing_job_adapter.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"

#include <gui/processing/qgsprocessingguiregistry.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>
#include <gui/qgsgui.h>

#include <qgsprocessingfeedback.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingalgorithm.h>
#include <qgsexception.h>
#include <qgsprocessingparameters.h>
#include <qgsprocessingprovider.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <gui/history/qgshistoryproviderregistry.h>
#include <gui/processing/qgsprocessingrecentalgorithmlog.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QSizePolicy>
#include <QGroupBox>
#include <QLabel>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QSettings>
#include <QVBoxLayout>

#include "providers/gdal_tools/gdal_tool_wrapper.h"
#include "providers/otb_tools/otb_tool_wrapper.h"
#include "providers/generic_cli/generic_cli_algorithm.h"


// ---------------------------------------------------------------------------
// Run-state (unique_ptr needs complete QgsProcessingAlgorithm in this TU)
// ---------------------------------------------------------------------------

SicnuProcessingRunState::SicnuProcessingRunState() = default;
SicnuProcessingRunState::~SicnuProcessingRunState() = default;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SicnuAlgorithmDialog::SicnuAlgorithmDialog( QWidget *parent )
  : QgsProcessingAlgorithmDialogBase( parent )
{
  if ( QgsProject *project = QgsProject::instance() )
  {
    mContext.setProject( project );
    mContext.setTransformContext( project->transformContext() );
  }
}

SicnuAlgorithmDialog::~SicnuAlgorithmDialog()
{
  if ( mJobHandle.isRunning() )
  {
    mJobHandle.cancel();
  }
  // Wrappers may have been parented to widgets in the form layout.
  // Only delete those that are still orphan (no parent).
  for ( auto *wrapper : mWrappers )
  {
    if ( wrapper && !wrapper->parent() )
      delete wrapper;
  }
  delete mFeedback;
  mFeedback = nullptr;
}

// ---------------------------------------------------------------------------
// Build parameter widgets using QGIS wrapper system
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::buildParameterWidgets()
{
  if ( !algorithm() )
    return;

  QgsProcessingParameterWidgetContext widgetContext;
  widgetContext.registerProcessingContextGenerator( this );

  if ( QgsProject *project = QgsProject::instance() )
  {
    widgetContext.setProject( project );
    mContext.setProject( project );
    mContext.setTransformContext( project->transformContext() );
  }

  QWidget *w = parentWidget();
  while ( w )
  {
    if ( QgsMapCanvas *canvas = w->findChild<QgsMapCanvas *>() )
    {
      widgetContext.setMapCanvas( canvas );
      widgetContext.setActiveLayer( canvas->currentLayer() );
      break;
    }
    w = w->parentWidget();
  }

  auto *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable( true );
  scrollArea->setFrameShape( QFrame::NoFrame );

  auto *container = new QWidget();
  auto *formLayout = new QFormLayout( container );
  formLayout->setLabelAlignment( Qt::AlignRight | Qt::AlignVCenter );
  formLayout->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );
  formLayout->setContentsMargins( 12, 10, 12, 10 );
  formLayout->setHorizontalSpacing( 12 );
  formLayout->setVerticalSpacing( 10 );

  auto *advancedGroup = new QGroupBox( tr( "高级参数" ) );
  advancedGroup->setObjectName( QStringLiteral( "rsDialogGroup" ) );
  auto *advancedLayout = new QFormLayout( advancedGroup );
  advancedLayout->setLabelAlignment( Qt::AlignRight );
  advancedLayout->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );
  advancedGroup->hide();

  const auto paramDefs = algorithm()->parameterDefinitions();
  for ( const QgsProcessingParameterDefinition *param : paramDefs )
  {
    if ( !param )
      continue;

    // Include destinations so OUTPUT paths are collected and loadable.
    QgsAbstractProcessingParameterWidgetWrapper *wrapper =
      QgsGui::processingGuiRegistry()->createParameterWidgetWrapper(
        param, Qgis::ProcessingMode::Standard );

    if ( !wrapper )
      continue;

    wrapper->setWidgetContext( widgetContext );
    wrapper->registerProcessingContextGenerator( this );
    wrapper->registerProcessingParametersGenerator( this );
    QWidget *widget = wrapper->createWrappedWidget( mContext );
    if ( !widget )
    {
      delete wrapper;
      continue;
    }

    QLabel *label = new QLabel( param->description() + QStringLiteral( ":" ) );
    const QString paramTip = param->toolTip().isEmpty()
                               ? param->description()
                               : param->toolTip();
    label->setToolTip( paramTip );
    label->setWhatsThis( paramTip );
    label->setStatusTip( paramTip );
    widget->setToolTip( paramTip );
    widget->setWhatsThis( paramTip );
    widget->setStatusTip( paramTip );

    widget->setMinimumWidth( 320 );
    widget->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );

    if ( param->flags() & Qgis::ProcessingParameterFlag::Advanced )
    {
      advancedLayout->addRow( label, widget );
      advancedGroup->show();
    }
    else
    {
      formLayout->addRow( label, widget );
    }

    mWrappers.append( wrapper );

    connect( wrapper, &QgsAbstractProcessingParameterWidgetWrapper::widgetValueHasChanged,
             this, [this]( QgsAbstractProcessingParameterWidgetWrapper * ) {
               updateCommandPreview();
             } );
  }

  if ( advancedGroup->isVisible() )
    formLayout->addWidget( advancedGroup );

  QSettings settings;
  mLoadResultsCheck = new QCheckBox( tr( "Load result layers into map" ) );
  mLoadResultsCheck->setChecked(
    settings.value( QStringLiteral( "processing/loadResultsToLayers" ), true ).toBool() );
  mLoadResultsCheck->setToolTip(
    tr( "When enabled, output rasters and vectors are added to the layer list after the tool finishes." ) );
  formLayout->addRow( mLoadResultsCheck );

  // GDAL / OTB / Generic CLI: live command-line preview from current parameters.
  mCommandGroup = new QGroupBox( tr( "调用命令 (Command)" ) );
  mCommandGroup->setObjectName( QStringLiteral( "rsAlgCommandPreviewGroup" ) );
  mCommandGroup->setToolTip( tr(
    "根据上方参数实时生成的外部命令行。可复制到终端手动执行（路径与临时输出可能与实际运行略有差异）。" ) );
  auto *cmdLayout = new QVBoxLayout( mCommandGroup );
  cmdLayout->setContentsMargins( 6, 6, 6, 6 );
  mCommandPreview = new QPlainTextEdit( mCommandGroup );
  mCommandPreview->setObjectName( QStringLiteral( "rsAlgCommandPreview" ) );
  mCommandPreview->setReadOnly( true );
  mCommandPreview->setLineWrapMode( QPlainTextEdit::WidgetWidth );
  mCommandPreview->setMaximumBlockCount( 50 );
  mCommandPreview->setMinimumHeight( 72 );
  mCommandPreview->setMaximumHeight( 140 );
  QFont mono = mCommandPreview->font();
  mono.setFamily( QStringLiteral( "monospace" ) );
  mono.setStyleHint( QFont::Monospace );
  mCommandPreview->setFont( mono );
  mCommandPreview->setPlaceholderText( tr( "（根据参数生成调用命令…）" ) );
  mCommandPreview->setToolTip( mCommandGroup->toolTip() );
  cmdLayout->addWidget( mCommandPreview );
  formLayout->addRow( mCommandGroup );

  const bool isCli =
    dynamic_cast<const GdalToolWrapper *>( algorithm() )
    || dynamic_cast<const OtbToolWrapper *>( algorithm() )
    || dynamic_cast<const GenericCliAlgorithm *>( algorithm() );
  mCommandGroup->setVisible( isCli );

  scrollArea->setWidget( container );

  auto *panelWidget = new QgsPanelWidget();
  auto *panelLayout = new QVBoxLayout( panelWidget );
  panelLayout->setContentsMargins( 0, 0, 0, 0 );
  panelLayout->addWidget( scrollArea );

  setMainWidget( panelWidget );

  if ( isCli )
    updateCommandPreview();

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "processing_algorithm" ) );
  // Append algorithm-specific short help into window WhatsThis
  if ( algorithm() )
  {
    const QString algHelp = algorithm()->shortHelpString().isEmpty()
                              ? algorithm()->shortDescription()
                              : algorithm()->shortHelpString();
    if ( !algHelp.isEmpty() )
    {
      setWhatsThis( SicnuDialogHelp::htmlForTool(
                      QStringLiteral( "processing_algorithm" ), algorithm()->displayName() )
                    + QStringLiteral( "<hr/>" ) + algHelp );
      setToolTip( algorithm()->shortDescription().isEmpty()
                    ? algorithm()->displayName()
                    : algorithm()->shortDescription() );
    }
  }
}

void SicnuAlgorithmDialog::updateCommandPreview()
{
  if ( !mCommandPreview || !mCommandGroup || !mCommandGroup->isVisible() || !algorithm() )
    return;

  const QVariantMap params = createProcessingParameters();
  QString cmd;

  if ( auto *gdal = dynamic_cast<GdalToolWrapper *>(
         const_cast<QgsProcessingAlgorithm *>( algorithm() ) ) )
  {
    cmd = gdal->commandLinePreview( params, mContext );
  }
  else if ( auto *otb = dynamic_cast<OtbToolWrapper *>(
              const_cast<QgsProcessingAlgorithm *>( algorithm() ) ) )
  {
    cmd = otb->commandLinePreview( params, mContext );
  }
  else if ( auto *cli = dynamic_cast<const GenericCliAlgorithm *>( algorithm() ) )
  {
    cmd = cli->commandLinePreview( params, mContext );
  }
  else
  {
    cmd = tr( "# 此算法为内置实现，无外部 CLI 命令" );
  }

  // Avoid resetting cursor if unchanged
  if ( mCommandPreview->toPlainText() != cmd )
    mCommandPreview->setPlainText( cmd );
}

// ---------------------------------------------------------------------------
// createProcessingParameters — collect values from all wrappers
// ---------------------------------------------------------------------------

QVariantMap SicnuAlgorithmDialog::createProcessingParameters( Flags )
{
  QVariantMap params;
  const bool loadResults = mLoadResultsCheck && mLoadResultsCheck->isChecked();
  QgsProject *destProject = loadResults ? QgsProject::instance() : nullptr;

  for ( const QgsAbstractProcessingParameterWidgetWrapper *wrapper : mWrappers )
  {
    if ( !wrapper || !wrapper->parameterDefinition() )
      continue;

    const QString paramName = wrapper->parameterDefinition()->name();
    QVariant value = wrapper->parameterValue();

    if ( value.userType() == qMetaTypeId<QgsProcessingOutputLayerDefinition>() )
    {
      QgsProcessingOutputLayerDefinition def = value.value<QgsProcessingOutputLayerDefinition>();
      def.destinationProject = loadResults ? destProject : nullptr;
      value = QVariant::fromValue( def );
    }

    params[paramName] = value;
  }

  return params;
}

// ---------------------------------------------------------------------------
// processingContext
// ---------------------------------------------------------------------------

QgsProcessingContext *SicnuAlgorithmDialog::processingContext()
{
  return &mContext;
}

// ---------------------------------------------------------------------------
// runAlgorithm — Task Center-backed async execution
// prepare() on GUI thread; runPrepared() on worker; postProcess() on GUI.
// ---------------------------------------------------------------------------

bool SicnuAlgorithmDialog::isFinalized()
{
  // Keep the dialog alive while a Task Center job is outstanding (no QgsTask).
  if ( mJobHandle.isRunning() )
    return false;
  return QgsProcessingAlgorithmDialogBase::isFinalized();
}

void SicnuAlgorithmDialog::closeEvent( QCloseEvent *event )
{
  if ( mJobHandle.isRunning() )
  {
    // #339: prevent WA_DeleteOnClose from destroying mContext/feedback while
    // the worker still dereferences them. Cancel and ignore the close;
    // the user can close again after the job finishes/cancels.
    mJobHandle.cancel();
    if ( mFeedback )
      mFeedback->pushInfo( tr( "Cancel requested — please wait for the task to finish." ) );
    event->ignore();
    return;
  }
  QgsProcessingAlgorithmDialogBase::closeEvent( event );
}

void SicnuAlgorithmDialog::runAlgorithm()
{
  if ( !algorithm() )
    return;

  if ( mJobHandle.isRunning() )
    return;

  if ( mLoadResultsCheck )
  {
    QSettings settings;
    settings.setValue( QStringLiteral( "processing/loadResultsToLayers" ),
                       mLoadResultsCheck->isChecked() );
  }

  QVariantMap params = createProcessingParameters();
  params = algorithm()->preprocessParameters( params );

  QString errorMsg;
  if ( !algorithm()->checkParameterValues( params, mContext, &errorMsg ) )
  {
    QMessageBox::warning( this, tr( "Invalid Parameters" ), errorMsg );
    return;
  }

  delete mFeedback;
  mFeedback = createFeedback();
  QgsProcessingFeedback *feedback = mFeedback;

  applyContextOverrides( &mContext );
  // Result layer loading is handled by the TaskCenter autoLoad path (see
  // submitJob below). Clear any framework-level completion layers so a stale
  // destinationProject does not introduce a second load.
  mContext.setLayersToLoadOnCompletion( {} );

  blockControlsWhileRunning();
  setExecutedAnyResult( true );
  cancelButton()->setEnabled(
    algorithm()->flags() & Qgis::ProcessingAlgorithmFlag::CanCancel );
  showLog();

  feedback->pushVersionInfo( algorithm()->provider() );
  if ( algorithm()->provider() )
  {
    const QString warn = algorithm()->provider()->warningMessage();
    if ( !warn.isEmpty() )
      feedback->reportError( warn );
  }

  mStartTime = QDateTime::currentMSecsSinceEpoch();
  feedback->pushInfo( tr( "Algorithm started at: %1" )
                        .arg( QDateTime::currentDateTime().toString( Qt::ISODate ) ) );
  feedback->setProgressText( tr( "<b>Algorithm '%1' starting&hellip;</b>" )
                               .arg( algorithm()->displayName() ) );

  feedback->pushInfo( tr( "Input parameters:" ) );
  QStringList paramParts;
  const auto paramDefs = algorithm()->parameterDefinitions();
  for ( const QgsProcessingParameterDefinition *param : paramDefs )
  {
    if ( !param || !params.contains( param->name() ) )
      continue;
    bool ok = false;
    const QString valStr = param->valueAsString( params.value( param->name() ), mContext, ok );
    paramParts.append( QStringLiteral( "'%1' : %2" )
                         .arg( param->name(), ok ? valStr : params.value( param->name() ).toString() ) );
  }
  feedback->pushCommandInfo( QStringLiteral( "{ %1 }" ).arg( paramParts.join( QStringLiteral( ", " ) ) ) );
  feedback->pushInfo( QString() );

  QVariantMap historyDetails;
  historyDetails[QStringLiteral( "algorithm_id" )] = algorithm()->id();
  historyDetails[QStringLiteral( "parameters" )] = algorithm()->asMap( params, mContext );
  const QString pythonCmd = algorithm()->asPythonCommand( params, mContext );
  if ( !pythonCmd.isEmpty() )
    historyDetails[QStringLiteral( "python_command" )] = pythonCmd;

  bool historyOk = false;
  mHistoryLogId = QgsGui::historyProviderRegistry()->addEntry(
    QStringLiteral( "processing" ), historyDetails, historyOk );
  mHistoryDetails = historyDetails;

  QgsGui::processingRecentAlgorithmLog()->push( algorithm()->id() );

  // prepare on GUI thread (context affinity), then Task Center runs runPrepared.
  auto state = std::make_shared<SicnuProcessingRunState>();
  state->parameters = params;
  state->algorithm.reset( algorithm()->create() );
  if ( !state->algorithm )
  {
    feedback->reportError( tr( "Failed to create algorithm instance." ) );
    resetGui();
    delete mFeedback;
    mFeedback = nullptr;
    return;
  }

  try
  {
    if ( !state->algorithm->prepare( params, mContext, feedback ) )
    {
      feedback->reportError( tr( "Algorithm prepare() failed." ) );
      resetGui();
      delete mFeedback;
      mFeedback = nullptr;
      mRunState.reset();
      return;
    }
  }
  catch ( const QgsProcessingException &e )
  {
    feedback->reportError( e.what() );
    resetGui();
    delete mFeedback;
    mFeedback = nullptr;
    mRunState.reset();
    return;
  }

  // #339: worker owns its own context so dialog's mContext can be destroyed safely.
  state->context = std::make_unique<QgsProcessingContext>();
  state->context->setProject( mContext.project() );
  mRunState = state;

  sicnu::jobs::JobRequest req;
  req.algorithmId = ProcessingJobAdapter::processingAlgorithmId( algorithm()->id() );
  req.title = algorithm()->displayName().toStdString();
  req.source = "toolbox";

  const bool autoLoad = !mLoadResultsCheck || mLoadResultsCheck->isChecked();
  QPointer<QgsProcessingFeedback> feedbackGuard(feedback);

  const long taskId = mJobHandle.submitJob(
    req,
    [state, feedbackGuard]( const sicnu::jobs::JobRequest &,
                            sicnu::operators::RSOperatorContext &ctx ) mutable {
      ctx.logInfo( "Running QgsProcessingAlgorithm (runPrepared)" );
      if ( feedbackGuard && feedbackGuard->isCanceled() )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      ctx.throwIfCancelled();

      state->workerStarted = true;

      try
      {
        // QPointer auto-nulls if dialog destroyed; pass raw pointer safely.
        QgsProcessingFeedback *fb = feedbackGuard.data();
        state->results = state->algorithm->runPrepared(
          state->parameters, *state->context, fb );
      }
      catch ( const QgsProcessingException &e )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::QgisProcessingError, e.what().toStdString() );
      }

      if ( ( feedbackGuard && feedbackGuard->isCanceled() ) || ctx.isCancelled() )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }

      return ProcessingJobAdapter::resultsToJson( state->results );
    },
    [feedbackGuard]() mutable {
      if ( feedbackGuard )
        feedbackGuard->cancel();
    },
    autoLoad,
    [this]( const QString &, const Json::Value & ) {
      bool successful = true;
      QVariantMap results;
      if ( mRunState && mRunState->algorithm && mRunState->workerStarted )
      {
        try
        {
          const QVariantMap pp = mRunState->algorithm->postProcess(
            mContext, mFeedback, successful );
          results = !pp.isEmpty() ? pp : mRunState->results;
        }
        catch ( const QgsProcessingException &e )
        {
          if ( mFeedback )
            mFeedback->reportError( e.what() );
          successful = false;
          results = mRunState->results;
        }
      }
      else if ( mRunState )
      {
        results = mRunState->results;
      }
      algExecuted( successful, results );
      mRunState.reset();
    },
    [this]( const QString &err, bool ) {
      bool successful = false;
      QVariantMap results = mRunState ? mRunState->results : QVariantMap();
      if ( mRunState && mRunState->algorithm && mRunState->workerStarted )
      {
        try
        {
          const QVariantMap pp = mRunState->algorithm->postProcess(
            mContext, mFeedback, successful );
          if ( !pp.isEmpty() )
            results = pp;
        }
        catch ( const QgsProcessingException &e )
        {
          if ( mFeedback )
            mFeedback->reportError( e.what() );
        }
      }
      if ( mFeedback && !err.isEmpty() && mFeedback->textLog().isEmpty() )
      {
        mFeedback->reportError( err );
      }
      algExecuted( successful, results );
      mRunState.reset();
    }
  );

  if ( feedback && taskId > 0 )
  {
    connect( feedback, &QgsFeedback::progressChanged, this, [taskId]( double progress ) {
      sicnu::TaskCenter::instance().updateTaskProgress( taskId, progress / 100.0 );
    } );
  }

  if ( feedback && taskId > 0 )
  {
    feedback->pushInfo( tr( "Submitted as task %1" ).arg( taskId ) );
  }
}

// ---------------------------------------------------------------------------
// algExecuted — async task completed; invoke post-processing (layer load, etc.)
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::algExecuted( bool successful, const QVariantMap &results )
{
  if ( mFeedback )
    finished( successful, results, mContext, mFeedback );

  // Result layers are loaded by the TaskCenter autoLoad path (submitJob was
  // called with autoLoad = mLoadResultsCheck->isChecked()). Loading them again
  // here duplicated the layer or loaded the temp path after it was moved
  // (P0-L2).
  Q_UNUSED( results )

  QgsProcessingAlgorithmDialogBase::algExecuted( successful, results );

  resetGui();
  delete mFeedback;
  mFeedback = nullptr;
}

// ---------------------------------------------------------------------------
// finished — called after algorithm completes
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::finished( bool successful, const QVariantMap &result,
                                     QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
  QgsProcessingAlgorithmDialogBase::finished( successful, result, context, feedback );

  if ( successful )
  {
    if ( feedback && mStartTime > 0 )
    {
      const double elapsed = ( QDateTime::currentMSecsSinceEpoch() - mStartTime ) / 1000.0;
      feedback->pushInfo( tr( "Execution completed in %1 seconds" ).arg( elapsed, 0, 'f', 2 ) );
    }

    if ( feedback )
      feedback->pushFormattedResults( algorithm(), context, result );
  }
  else if ( feedback )
  {
    if ( mStartTime > 0 )
    {
      const double elapsed = ( QDateTime::currentMSecsSinceEpoch() - mStartTime ) / 1000.0;
      feedback->reportError( tr( "Execution failed after %1 seconds" ).arg( elapsed, 0, 'f', 2 ) );
    }
    feedback->reportError( tr( "Algorithm failed." ) );
  }

  if ( mHistoryLogId >= 0 )
  {
    mHistoryDetails[QStringLiteral( "results" )] = result;
    if ( feedback )
      mHistoryDetails[QStringLiteral( "log" )] = feedback->htmlLog();
    QgsGui::historyProviderRegistry()->updateEntry( mHistoryLogId, mHistoryDetails );
  }
}
