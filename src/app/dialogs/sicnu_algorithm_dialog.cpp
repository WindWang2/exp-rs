// sicnu_algorithm_dialog.cpp — Phase: Processing Toolbox overhaul
#include "sicnu_algorithm_dialog.h"
#include "dialog_help_catalog.h"
#include "main_window.h"

#include <gui/processing/qgsprocessingguiregistry.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>
#include <gui/qgsgui.h>

#include <qgsprocessingalgrunnertask.h>
#include <qgsprocessingfeedback.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingalgorithm.h>
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

namespace
{

QString outputPathFromVariant( const QVariant &value )
{
  if ( value.userType() == qMetaTypeId<QgsProcessingOutputLayerDefinition>() )
    return value.value<QgsProcessingOutputLayerDefinition>().sink.staticValue().toString();

  return value.toString();
}

QString normalizedLayerSource( const QString &source )
{
  if ( source.isEmpty() )
    return source;

  const QFileInfo fi( source );
  if ( fi.exists() )
    return fi.canonicalFilePath();

  return source;
}

bool projectHasLayerWithSource( const QString &source )
{
  const QString normalized = normalizedLayerSource( source );
  const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( QgsMapLayer *layer : layers )
  {
    if ( !layer )
      continue;
    if ( layer->source() == source
         || normalizedLayerSource( layer->source() ) == normalized )
      return true;
  }
  return false;
}

/**
 * If a temporary destination was re-resolved after the algorithm wrote the file,
 * the reported path can point at an empty sibling dir under processing_XXXXXX.
 * Search for the same basename under that processing root.
 */
QString findSiblingTempOutput( const QString &reportedPath )
{
  if ( reportedPath.isEmpty() )
    return {};

  const QFileInfo fi( reportedPath );
  const QString baseName = fi.fileName();
  if ( baseName.isEmpty() )
    return {};

  // Expect .../processing_XXXXXX/<uuid>/OUTPUT.tif
  const QDir runDir = fi.dir();
  QDir rootDir = runDir;
  if ( !rootDir.cdUp() )
    return {};

  if ( !rootDir.dirName().startsWith( QStringLiteral( "processing_" ) ) )
    return {};

  const QFileInfoList subdirs = rootDir.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot );
  QString bestPath;
  qint64 bestSize = -1;
  for ( const QFileInfo &sub : subdirs )
  {
    const QString candidate = sub.absoluteFilePath() + QLatin1Char( '/' ) + baseName;
    const QFileInfo cfi( candidate );
    if ( !cfi.exists() || !cfi.isFile() )
      continue;
    if ( cfi.size() > bestSize )
    {
      bestSize = cfi.size();
      bestPath = cfi.canonicalFilePath();
    }
  }
  return bestPath;
}

QString resolvedResultPath(
  const QgsProcessingParameterDefinition *param,
  const QVariant &value,
  QgsProcessingContext &context )
{
  const QString direct = outputPathFromVariant( value );
  if ( !direct.isEmpty() && QFileInfo::exists( direct ) )
    return direct;

  // Recover when TEMPORARY_OUTPUT was resolved more than once (new UUID dir).
  if ( !direct.isEmpty() )
  {
    const QString sibling = findSiblingTempOutput( direct );
    if ( !sibling.isEmpty() )
      return sibling;
  }

  if ( param )
  {
    // testOnly=true: do not register layers; still may generate a NEW temp path
    // if value is still the TEMPORARY_OUTPUT token — only accept if the file exists.
    const QString resolved = QgsProcessingParameters::parameterAsOutputLayer(
                               param, value, context, true );
    if ( !resolved.isEmpty() && QFileInfo::exists( resolved ) )
      return resolved;

    if ( !resolved.isEmpty() )
    {
      const QString sibling = findSiblingTempOutput( resolved );
      if ( !sibling.isEmpty() )
        return sibling;
    }
  }

  return direct;
}

QgisDesktopWindow *findMainWindow( QWidget *start )
{
  for ( QWidget *w = start; w; w = w->parentWidget() )
  {
    if ( auto *mainWin = qobject_cast<QgisDesktopWindow *>( w ) )
      return mainWin;
  }
  return nullptr;
}

bool isLoadableDestinationType( const QString &type )
{
  return type == QgsProcessingParameterRasterDestination::typeName()
         || type == QgsProcessingParameterVectorDestination::typeName()
         || type == QgsProcessingParameterFeatureSink::typeName();
}

bool addRasterToProject( const QString &path, QgsProcessingFeedback *feedback )
{
  const QFileInfo fi( path );
  auto *layer = new QgsRasterLayer( path, fi.fileName(), QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    if ( feedback )
    {
      feedback->reportError(
        QObject::tr( "Failed to load result raster: %1" )
          .arg( layer->error().message() ) );
    }
    delete layer;
    return false;
  }

  QgsProject *project = QgsProject::instance();
  project->addMapLayer( layer, false );
  QgsLayerTreeGroup *group = project->layerTreeRoot()->findGroup( QStringLiteral( "Raster Layers" ) );
  if ( !group )
    group = project->layerTreeRoot()->insertGroup( 0, QStringLiteral( "Raster Layers" ) );
  group->addLayer( layer );
  return true;
}

bool addVectorToProject( const QString &path, QgsProcessingFeedback *feedback )
{
  const QFileInfo fi( path );
  auto *layer = new QgsVectorLayer( path, fi.fileName(), QStringLiteral( "ogr" ) );
  if ( !layer->isValid() )
  {
    if ( feedback )
    {
      feedback->reportError(
        QObject::tr( "Failed to load result vector: %1" )
          .arg( layer->error().message() ) );
    }
    delete layer;
    return false;
  }

  QgsProject *project = QgsProject::instance();
  project->addMapLayer( layer, false );
  QgsLayerTreeGroup *group = project->layerTreeRoot()->findGroup( QStringLiteral( "Vector Layers" ) );
  if ( !group )
    group = project->layerTreeRoot()->insertGroup( 0, QStringLiteral( "Vector Layers" ) );
  group->addLayer( layer );
  return true;
}

int loadFileResultLayers(
  const QgsProcessingAlgorithm *algorithm,
  const QVariantMap &result,
  QgsProcessingContext &context,
  QgsProcessingFeedback *feedback,
  QWidget *parentWidget )
{
  if ( !algorithm )
    return 0;

  QgisDesktopWindow *mainWin = findMainWindow( parentWidget );
  int loadedCount = 0;

  for ( const QgsProcessingParameterDefinition *param : algorithm->parameterDefinitions() )
  {
    if ( !param || !result.contains( param->name() ) )
      continue;

    if ( !param->isDestination() )
      continue;

    const QString type = param->type();
    if ( !isLoadableDestinationType( type ) )
    {
      if ( feedback )
      {
        feedback->pushWarning(
          QObject::tr( "Skipping auto-load for unsupported output type '%1' (%2)." )
            .arg( type, param->name() ) );
      }
      continue;
    }

    const QString path = resolvedResultPath( param, result.value( param->name() ), context );
    if ( path.isEmpty() )
      continue;

    if ( !QFileInfo::exists( path ) )
    {
      if ( feedback )
      {
        feedback->reportError(
          QObject::tr( "Output file not found: %1" ).arg( path ) );
      }
      continue;
    }

    if ( projectHasLayerWithSource( path ) )
      continue;

    bool loaded = false;
    if ( type == QgsProcessingParameterRasterDestination::typeName() )
    {
      if ( mainWin )
      {
        mainWin->loadRasterLayer( path );
        loaded = projectHasLayerWithSource( path );
        if ( !loaded )
          loaded = addRasterToProject( path, feedback );
      }
      else
      {
        loaded = addRasterToProject( path, feedback );
      }
    }
    else // vector / feature sink
    {
      if ( mainWin )
      {
        mainWin->loadVectorLayer( path );
        loaded = projectHasLayerWithSource( path );
        if ( !loaded )
          loaded = addVectorToProject( path, feedback );
      }
      else
      {
        loaded = addVectorToProject( path, feedback );
      }
    }

    if ( !loaded )
      continue;

    if ( feedback )
      feedback->pushInfo( QObject::tr( "Loaded result layer: %1" ).arg( path ) );
    ++loadedCount;
  }

  return loadedCount;
}

} // namespace

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
  // Wrappers may have been parented to widgets in the form layout.
  // Only delete those that are still orphan (no parent).
  for ( auto *wrapper : mWrappers )
  {
    if ( wrapper && !wrapper->parent() )
      delete wrapper;
  }
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
  formLayout->setLabelAlignment( Qt::AlignRight );
  formLayout->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );
  formLayout->setContentsMargins( 4, 4, 4, 4 );

  auto *advancedGroup = new QGroupBox( tr( "Advanced Parameters" ) );
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
// runAlgorithm — async execution via QgsProcessingAlgRunnerTask
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::runAlgorithm()
{
  if ( !algorithm() )
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

  mFeedback = createFeedback();
  QgsProcessingFeedback *feedback = mFeedback;

  applyContextOverrides( &mContext );
  // File-based CLI tools load via loadFileResultLayers; clear framework queue
  // so we do not double-load when destinationProject is also set.
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

  QgsProcessingAlgRunnerTask *task = new QgsProcessingAlgRunnerTask(
    algorithm(), params, mContext, feedback );

  setCurrentTask( task );
}

// ---------------------------------------------------------------------------
// algExecuted — async task completed; invoke post-processing (layer load, etc.)
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::algExecuted( bool successful, const QVariantMap &results )
{
  if ( mFeedback )
    finished( successful, results, mContext, mFeedback );

  if ( successful && algorithm() && mLoadResultsCheck && mLoadResultsCheck->isChecked() )
  {
    loadFileResultLayers( algorithm(), results, mContext, mFeedback, parentWidget() );
  }

  QgsProcessingAlgorithmDialogBase::algExecuted( successful, results );

  resetGui();
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
