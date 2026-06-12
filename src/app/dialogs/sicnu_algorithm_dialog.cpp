// sicnu_algorithm_dialog.cpp — Phase: Processing Toolbox overhaul
#include "sicnu_algorithm_dialog.h"

#include <gui/processing/qgsprocessingguiregistry.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>
#include <gui/processing/qgsprocessingguiutils.h>
#include <gui/qgsgui.h>

#include <qgsprocessingalgrunnertask.h>
#include <qgsprocessingfeedback.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingalgorithm.h>
#include <qgsprocessingparameters.h>
#include <qgsprocessingutils.h>
#include <qgsprocessingprovider.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgslayertreegroup.h>
#include <gui/history/qgshistoryproviderregistry.h>
#include <gui/processing/qgsprocessingrecentalgorithmlog.h>

#include <QFormLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QLabel>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonDocument>

// ---------------------------------------------------------------------------
// C++ port of Postprocessing.py::handleAlgorithmResults()
//
// Uses the QGIS framework's layersToLoadOnCompletion() mechanism,
// QgsProcessingGuiUtils::addResultLayers() for proper layer tree placement,
// and respects layer sort keys and output group names.
// ---------------------------------------------------------------------------

static bool handleAlgorithmResults(
    const QgsProcessingAlgorithm *algorithm,
    QgsProcessingContext &context,
    QgsProcessingFeedback *feedback,
    const QVariantMap & )
{
    if ( !feedback )
        return true;

    feedback->setProgressText( QObject::tr( "Loading resulting layers" ) );

    QVector<QgsProcessingGuiUtils::ResultLayerDetails> addedLayers;
    const auto layersToLoad = context.layersToLoadOnCompletion();
    const int totalCount = layersToLoad.size();
    int i = 0;

    for ( auto it = layersToLoad.constBegin(); it != layersToLoad.constEnd(); ++it )
    {
        if ( feedback->isCanceled() )
            return false;

        if ( totalCount > 2 )
            feedback->setProgress( 100 * i / static_cast<double>( totalCount ) );

        const QString destId = it.key();
        const QgsProcessingContext::LayerDetails &details = it.value();

        std::unique_ptr<QgsMapLayer> layer(
            QgsProcessingUtils::mapLayerFromString( destId, context, false, details.layerTypeHint ) );
        if ( layer )
        {
            details.setOutputLayerName( layer.get() );

            // Transfer ownership from temporary store
            QgsMapLayer *ownedLayer = context.temporaryLayerStore()->takeMapLayer( layer.get() );
            if ( ownedLayer )
            {
                QgsProcessingGuiUtils::ResultLayerDetails resultDetails( ownedLayer );
                resultDetails.targetLayerTreeGroup =
                    QgsProcessingGuiUtils::layerTreeResultsGroup( details, context );
                resultDetails.sortKey = details.layerSortKey;
                resultDetails.destinationProject = details.project;
                addedLayers.append( resultDetails );
            }
        }
        ++i;
    }

    QgsProcessingGuiUtils::addResultLayers( addedLayers, context );
    feedback->setProgress( 100 );
    return true;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Static cache shared across all algorithm dialogs
sicnu::ProcessingCache SicnuAlgorithmDialog::s_cache;

SicnuAlgorithmDialog::SicnuAlgorithmDialog( QWidget *parent )
    : QgsProcessingAlgorithmDialogBase( parent )
{
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
// Cache key generation — SHA256 of algorithm ID + serialized parameters
// ---------------------------------------------------------------------------

QString SicnuAlgorithmDialog::computeCacheKey( const QVariantMap &params )
{
    if ( !algorithm() )
        return QString();

    QByteArray data;
    data.append( algorithm()->id().toUtf8() );
    data.append( '\0' );
    data.append( QJsonDocument::fromVariant( params ).toJson( QJsonDocument::Compact ) );

    QByteArray hash = QCryptographicHash::hash( data, QCryptographicHash::Sha256 );
    return QString::fromLatin1( hash.toHex().left( 16 ) ); // 16 hex chars = 64 bits
}

// ---------------------------------------------------------------------------
// Build parameter widgets using QGIS wrapper system
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::buildParameterWidgets()
{
    if ( !algorithm() )
        return;

    // Set up widget context (links to project, canvas, etc.)
    QgsProcessingParameterWidgetContext widgetContext;
    widgetContext.setProject( QgsProject::instance() );

    // Set map canvas so layer combo boxes can resolve CRS/extent
    QWidget *w = parentWidget();
    while ( w )
    {
      if ( QgsMapCanvas *canvas = w->findChild<QgsMapCanvas *>() )
      {
        widgetContext.setMapCanvas( canvas );
        break;
      }
      w = w->parentWidget();
    }

    // Create a scroll area with a form layout for parameters
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable( true );
    scrollArea->setFrameShape( QFrame::NoFrame );

    auto *container = new QWidget();
    auto *formLayout = new QFormLayout( container );
    formLayout->setLabelAlignment( Qt::AlignRight );
    formLayout->setContentsMargins( 4, 4, 4, 4 );

    // Advanced parameters group
    auto *advancedGroup = new QGroupBox( tr( "Advanced Parameters" ) );
    auto *advancedLayout = new QFormLayout( advancedGroup );
    advancedLayout->setLabelAlignment( Qt::AlignRight );
    advancedGroup->hide();

    const auto paramDefs = algorithm()->parameterDefinitions();
    for ( const QgsProcessingParameterDefinition *param : paramDefs )
    {
        if ( !param )
            continue;

        // Skip destination parameters — they're handled by the base dialog
        if ( param->isDestination() )
            continue;

        QgsAbstractProcessingParameterWidgetWrapper *wrapper =
            QgsGui::processingGuiRegistry()->createParameterWidgetWrapper(
                param, Qgis::ProcessingMode::Standard );

        if ( !wrapper )
            continue;

        wrapper->setWidgetContext( widgetContext );
        QWidget *widget = wrapper->createWrappedWidget( mContext );
        if ( !widget )
        {
            delete wrapper;
            continue;
        }

        // Create label
        QLabel *label = new QLabel( param->description() + QStringLiteral( ":" ) );
        label->setToolTip( param->toolTip() );

        // Add to form or advanced group
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
    }

    // Add advanced group at the bottom
    if ( advancedGroup->isVisible() )
        formLayout->addWidget( advancedGroup );

    scrollArea->setWidget( container );

    // Create a simple panel widget to hold the scroll area
    auto *panelWidget = new QgsPanelWidget();
    auto *panelLayout = new QVBoxLayout( panelWidget );
    panelLayout->setContentsMargins( 0, 0, 0, 0 );
    panelLayout->addWidget( scrollArea );

    setMainWidget( panelWidget );
}

// ---------------------------------------------------------------------------
// createProcessingParameters — collect values from all wrappers
// ---------------------------------------------------------------------------

QVariantMap SicnuAlgorithmDialog::createProcessingParameters( Flags )
{
    QVariantMap params;

    for ( const QgsAbstractProcessingParameterWidgetWrapper *wrapper : mWrappers )
    {
        if ( !wrapper || !wrapper->parameterDefinition() )
            continue;

        const QString paramName = wrapper->parameterDefinition()->name();
        params[paramName] = wrapper->parameterValue();
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
// Aligned with QGIS Python AlgorithmDialog.runAlgorithm()
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::runAlgorithm()
{
    if ( !algorithm() )
        return;

    // 1. Collect parameters from widgets
    QVariantMap params = createProcessingParameters();

    // 2. Preprocess (resolve layer references, etc.)
    params = algorithm()->preprocessParameters( params );

    // 3. Validate
    QString errorMsg;
    if ( !algorithm()->checkParameterValues( params, mContext, &errorMsg ) )
    {
        QMessageBox::warning( this, tr( "Invalid Parameters" ), errorMsg );
        return;
    }

    // 3.5. Check processing cache for repeated execution
    QString cacheKey = computeCacheKey( params );
    if ( !cacheKey.isEmpty() && s_cache.contains( cacheKey ) )
    {
        QByteArray cachedData = s_cache.retrieve( cacheKey );
        QVariantMap cachedResult = QJsonDocument::fromJson( cachedData ).toVariant().toMap();
        if ( !cachedResult.isEmpty() )
        {
            QgsProcessingFeedback *fb = createFeedback();
            fb->pushInfo( tr( "Result loaded from cache (key: %1)" ).arg( cacheKey ) );
            finished( true, cachedResult, mContext, fb );
            return;
        }
    }

    // 4. Create feedback connected to dialog's progress/log UI
    QgsProcessingFeedback *feedback = createFeedback();

    // 5. Apply context overrides (from dialog settings)
    applyContextOverrides( &mContext );

    // 6. Block UI, switch to log tab, enable cancel
    blockControlsWhileRunning();
    setExecutedAnyResult( true );
    cancelButton()->setEnabled(
        algorithm()->flags() & Qgis::ProcessingAlgorithmFlag::CanCancel );
    showLog();

    // 7. Push version info and provider warnings
    feedback->pushVersionInfo( algorithm()->provider() );
    if ( algorithm()->provider() )
    {
        const QString warn = algorithm()->provider()->warningMessage();
        if ( !warn.isEmpty() )
            feedback->reportError( warn );
    }

    // 8. Log algorithm start time and input parameters
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

    // 9. Record to processing history
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

    // 10. Record in recent algorithms
    QgsGui::processingRecentAlgorithmLog()->push( algorithm()->id() );

    // 11. Create and launch the async task
    QgsProcessingAlgRunnerTask *task = new QgsProcessingAlgRunnerTask(
        algorithm(), params, mContext, feedback );

    setCurrentTask( task ); // connects executed → algExecuted, adds to task manager
}

// ---------------------------------------------------------------------------
// finished — called after algorithm completes
// Aligned with QGIS Python AlgorithmDialog.finish() + Postprocessing.py
// ---------------------------------------------------------------------------

void SicnuAlgorithmDialog::finished( bool successful, const QVariantMap &result,
                                      QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsProcessingAlgorithmDialogBase::finished( successful, result, context, feedback );

    if ( successful )
    {
        // Report elapsed time
        if ( feedback && mStartTime > 0 )
        {
            const double elapsed = ( QDateTime::currentMSecsSinceEpoch() - mStartTime ) / 1000.0;
            feedback->pushInfo( tr( "Execution completed in %1 seconds" ).arg( elapsed, 0, 'f', 2 ) );
        }

        // Use QGIS framework mechanism to load result layers
        if ( algorithm() )
            handleAlgorithmResults( algorithm(), context, feedback, result );

        // Push formatted results summary
        if ( feedback )
            feedback->pushFormattedResults( algorithm(), context, result );

        // Store result in processing cache for repeated execution
        if ( algorithm() && !result.isEmpty() )
        {
            QVariantMap params = createProcessingParameters();
            params = algorithm()->preprocessParameters( params );
            QString cacheKey = computeCacheKey( params );
            if ( !cacheKey.isEmpty() )
            {
                QByteArray data = QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact );
                s_cache.store( cacheKey, data );
                if ( feedback )
                    feedback->pushInfo( tr( "Result cached (key: %1)" ).arg( cacheKey ) );
            }
        }
    }
    else
    {
        if ( feedback )
        {
            if ( mStartTime > 0 )
            {
                const double elapsed = ( QDateTime::currentMSecsSinceEpoch() - mStartTime ) / 1000.0;
                feedback->reportError( tr( "Execution failed after %1 seconds" ).arg( elapsed, 0, 'f', 2 ) );
            }
            feedback->reportError( tr( "Algorithm failed." ) );
        }
    }

    // Update processing history with results and log
    if ( mHistoryLogId >= 0 )
    {
        mHistoryDetails[QStringLiteral( "results" )] = result;
        if ( feedback )
            mHistoryDetails[QStringLiteral( "log" )] = feedback->htmlLog();
        QgsGui::historyProviderRegistry()->updateEntry( mHistoryLogId, mHistoryDetails );
    }
}
