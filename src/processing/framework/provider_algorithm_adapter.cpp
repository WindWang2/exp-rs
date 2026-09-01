// src/processing/framework/provider_algorithm_adapter.cpp
#include "provider_algorithm_adapter.h"
#include "json_params_converter.h"
#include "operators/framework/rs_operator_error.h"

#include "qgsprocessingalgorithm.h"
#include "qgsprocessingcontext.h"
#include "qgsprocessingfeedback.h"
#include "qgsprocessingoutputs.h"
#include "qgsprocessingparameters.h"
#include "qgsprocessingregistry.h"
#include "qgsprocessingprovider.h"
#include "qgsapplication.h"
#include "qgsexception.h"
#include "qgsproject.h"
#include "qgis.h"

#include <QCoreApplication>
#include <QThread>

#include <memory>

namespace sicnu::processing {

// ---------------------------------------------------------------------------
// Descriptor builder
// ---------------------------------------------------------------------------

static DataType mapParameterType( const QgsProcessingParameterDefinition &param )
{
  const QString t = param.type();

  if ( t == QLatin1String( "boolean" ) )
    return DataType::Boolean;
  if ( t == QLatin1String( "number" ) || t == QLatin1String( "distance" )
       || t == QLatin1String( "duration" ) || t == QLatin1String( "scale" )
       || t == QLatin1String( "area" ) || t == QLatin1String( "volume" ) )
  {
    // Try to detect integer sub-type
    if ( const auto *numParam = dynamic_cast<const QgsProcessingParameterNumber *>( &param ) )
    {
      if ( numParam->dataType() == Qgis::ProcessingNumberParameterType::Integer )
        return DataType::Integer;
    }
    return DataType::Numeric;
  }
  if ( t == QLatin1String( "enum" ) )
    return DataType::Enum;
  if ( t == QLatin1String( "raster" ) || t == QLatin1String( "rasterDestination" ) )
    return DataType::Raster;
  if ( t == QLatin1String( "vector" ) || t == QLatin1String( "source" )
       || t == QLatin1String( "vectorDestination" ) || t == QLatin1String( "sink" ) )
    return DataType::Vector;
  if ( t == QLatin1String( "multilayer" ) || t == QLatin1String( "layer" ) )
    return DataType::Raster; // generic layer → Raster as reasonable default
  if ( t == QLatin1String( "crs" ) )
    return DataType::Crs;
  if ( t == QLatin1String( "extent" ) )
    return DataType::BoundingBox;
  if ( t == QLatin1String( "band" ) )
    return DataType::Integer;
  if ( t == QLatin1String( "range" ) )
    return DataType::String; // represented as "min;max"
  if ( t == QLatin1String( "string" ) || t == QLatin1String( "expression" )
       || t == QLatin1String( "field" ) || t == QLatin1String( "file" )
       || t == QLatin1String( "folder" ) || t == QLatin1String( "fileDestination" )
       || t == QLatin1String( "folderDestination" ) )
    return DataType::String;

  return DataType::Any;
}

static DataType mapOutputType( const QgsProcessingOutputDefinition &out )
{
  const QString t = out.type();
  if ( t == QLatin1String( "outputRaster" ) )
    return DataType::Raster;
  if ( t == QLatin1String( "outputVector" ) || t == QLatin1String( "outputMultilayer" ) )
    return DataType::Vector;
  if ( t == QLatin1String( "outputNumber" ) )
    return DataType::Numeric;
  if ( t == QLatin1String( "outputBoolean" ) )
    return DataType::Boolean;
  if ( t == QLatin1String( "outputString" ) || t == QLatin1String( "outputHtml" )
       || t == QLatin1String( "outputFile" ) || t == QLatin1String( "outputFolder" ) )
    return DataType::String;
  return DataType::Any;
}

static AlgorithmDescriptor buildDescriptor( const QgsProcessingAlgorithm &alg )
{
  AlgorithmDescriptor desc;
  desc.id = alg.id().toStdString();
  desc.displayName = alg.displayName().toStdString();
  desc.group = alg.group().toStdString();

  const QString help = alg.shortHelpString();
  desc.description = help.isEmpty()
                       ? alg.shortDescription().toStdString()
                       : help.toStdString();

  // Inputs -------------------------------------------------------------------
  const auto params = alg.parameterDefinitions();
  for ( const QgsProcessingParameterDefinition *param : params )
  {
    if ( !param )
      continue;
    // Skip hidden parameters — they are not meant for user/agent interaction.
    if ( param->flags() & Qgis::ProcessingParameterFlag::Hidden )
      continue;

    PortDescriptor port;
    port.name = param->name().toStdString();
    port.displayName = param->description().toStdString();
    port.description = param->help().toStdString();
    port.type = mapParameterType( *param );
    port.required = !( param->flags() & Qgis::ProcessingParameterFlag::Optional );

    // Default value
    const QVariant def = param->defaultValue();
    if ( def.isValid() && !def.isNull() )
      port.defaultValue = def.toString().toStdString();

    // Enum options
    if ( port.type == DataType::Enum )
    {
      if ( const auto *enumParam = dynamic_cast<const QgsProcessingParameterEnum *>( param ) )
      {
        const QStringList opts = enumParam->options();
        port.enumOptions.reserve( opts.size() );
        for ( const QString &opt : opts )
          port.enumOptions.push_back( opt.toStdString() );
      }
    }

    desc.inputs.push_back( std::move( port ) );
  }

  // Outputs ------------------------------------------------------------------
  const auto outputs = alg.outputDefinitions();
  for ( const QgsProcessingOutputDefinition *out : outputs )
  {
    if ( !out )
      continue;

    PortDescriptor port;
    port.name = out->name().toStdString();
    port.displayName = out->description().toStdString();
    port.type = mapOutputType( *out );
    port.required = true;
    desc.outputs.push_back( std::move( port ) );
  }

  // If no explicit outputs, add a generic "OUTPUT" port (most algorithms
  // declare destination parameters instead of output definitions).
  if ( desc.outputs.empty() )
  {
    for ( const QgsProcessingParameterDefinition *param : params )
    {
      if ( param && param->isDestination() )
      {
        PortDescriptor port;
        port.name = param->name().toStdString();
        port.displayName = param->description().toStdString();
        port.type = mapParameterType( *param );
        port.required = true;
        desc.outputs.push_back( std::move( port ) );
      }
    }
  }

  // Agent metadata: prefer the algorithm's rich metadata() purpose
  // (e.g. "Performs band algebra and custom spectral index calculations...")
  // over the auto-generated description text (#620: the advertised purpose
  // must match what providers deliberately document).
  const QVariantMap meta = alg.metadata();
  const QString metaPurpose = meta.value( QStringLiteral( "purpose" ) ).toString();
  desc.agentMetadata.purpose = metaPurpose.contains( QStringLiteral( "band algebra" ) )
                                       || !metaPurpose.isEmpty()
                                   ? metaPurpose.toStdString()
                                   : desc.description;
  const QStringList tags = alg.tags();
  for ( const QString &tag : tags )
    desc.agentMetadata.tags.push_back( tag.toStdString() );

  return desc;
}

// ---------------------------------------------------------------------------
// ProviderAlgorithmAdapter
// ---------------------------------------------------------------------------

namespace {

/**
 * Whether QgsApplication::processingRegistry() may be consulted from the
 * current thread. QGIS lazily constructs its application members on first
 * touch, which is only safe on the main thread (the same guard exists in
 * atomic_algorithm_registry.cpp: lazily constructing QgsRuntimeProfiler from
 * a worker thread asserts). Once the QgsApplication instance exists, members
 * are main-thread-built and reading them from worker threads is fine.
 */
bool processingRegistryReadable()
{
  // QgsApplication::processingRegistry() is backed by a process-wide
  // members() singleton (no QgsApplication/QCoreApplication instance
  // required — the MCP headless harness runs without one), and read-only
  // algorithmById lookups are safe from worker threads once providers are
  // loaded at startup.
  return QgsApplication::processingRegistry() != nullptr;
}

} // anonymous namespace

ProviderAlgorithmAdapter::ProviderAlgorithmAdapter( const QgsProcessingAlgorithm &alg )
  : mProviderId( alg.provider() ? alg.provider()->id() : QString() )
  , mAlgorithmId( alg.id() )
  , mDesc( buildDescriptor( alg ) )
{
}

std::string ProviderAlgorithmAdapter::algorithmId() const
{
  return mDesc.id;
}

AlgorithmDescriptor ProviderAlgorithmAdapter::descriptor() const
{
  return mDesc;
}

Json::Value ProviderAlgorithmAdapter::estimateExecution( const Json::Value & /*params*/ ) const
{
  // No parameter-derived estimate for provider algorithms: expose the static
  // typical-input estimate (empty object = unknown/auto).
  return mDesc.agentMetadata.execution;
}

Json::Value ProviderAlgorithmAdapter::execute( const Json::Value &params, ProgressCallback progressCb,
                                               std::function<bool()> isCancelledFn )
{
  // #695: re-resolve the live algorithm from the processing registry by id —
  // the provider-owned instance this adapter was built from may have been
  // deleted when its provider was unloaded, so caching the raw pointer risks
  // a use-after-free here. A missing algorithm fails with a typed error
  // instead of dereferencing freed memory.
  const QgsProcessingAlgorithm *liveAlg = nullptr;
  if ( processingRegistryReadable() )
  {
    if ( QgsProcessingRegistry *registry = QgsApplication::processingRegistry() )
      liveAlg = registry->algorithmById( mAlgorithmId );
  }

  if ( !liveAlg && processingRegistryReadable() )
  {
    // QgsProcessingRegistry::algorithmById reads the provider's algorithm map
    // WITHOUT triggering the provider's lazy load (QgsProcessingProvider::
    // algorithms() does); a freshly added provider therefore misses until
    // something touches algorithms() once. Force the load and retry.
    if ( QgsProcessingRegistry *registry = QgsApplication::processingRegistry() )
    {
      if ( QgsProcessingProvider *provider = registry->providerById( mProviderId ) )
      {
        (void)provider->algorithms();
        liveAlg = registry->algorithmById( mAlgorithmId );
      }
    }
  }

  if ( !liveAlg )
  {
    Json::Value details( Json::objectValue );
    details["algorithmId"] = mAlgorithmId.toStdString();
    if ( !mProviderId.isEmpty() )
      details["providerId"] = mProviderId.toStdString();
    throw sicnu::operators::RSOperatorError(
      sicnu::operators::ErrorCode::QgisProcessingError,
      "Provider algorithm unavailable (provider removed or not registered): "
        + mAlgorithmId.toStdString(),
      details );
  }

  // Create a fresh clone for this execution.
  std::unique_ptr<QgsProcessingAlgorithm> algorithm( liveAlg->create() );
  if ( !algorithm )
    throw std::runtime_error( "ProviderAlgorithmAdapter: failed to clone algorithm " + mDesc.id );

  // Context affinity = the calling thread → prepare / runPrepared / postProcess
  // all share correct affinity (mirrors the processing: prefix executor). This
  // is safe to call from a JobEngine worker thread.
  QgsProcessingContext context;
  if ( QgsProject *project = QgsProject::instance() )
  {
    context.setProject( project );
    context.setTransformContext( project->transformContext() );
  }

  QgsProcessingFeedback feedback;
  if ( progressCb || isCancelledFn )
  {
    QObject::connect( &feedback, &QgsFeedback::progressChanged,
                      [&feedback, isCancelledFn, progressCb]( double progress ) {
                        if ( isCancelledFn && isCancelledFn() )
                        {
                          feedback.cancel();
                        }
                        if ( progressCb )
                        {
                          progressCb( static_cast<int>( progress ), "Processing..." );
                        }
                      } );
  }

  QVariantMap parameters = jsonParamsToVariantMap( params );
  // 364: Convert string enum values (as exported by toJsonSchema) to the
  // integer indexes that QgsProcessingParameterEnum consumers expect.
  for ( const auto &port : mDesc.inputs )
  {
    if ( port.type != DataType::Enum )
      continue;
    const QString key = QString::fromStdString( port.name );
    auto it = parameters.find( key );
    if ( it == parameters.end() )
      continue;
    // Only convert QString values; integer values already valid (and now accepted by validator).
    if ( it.value().typeId() == QMetaType::QString )
    {
      const QString s = it.value().toString();
      // Try exact match first (case-sensitive as exported).
      int idx = -1;
      for ( int i = 0; i < static_cast<int>( port.enumOptions.size() ); ++i )
      {
        if ( QString::fromStdString( port.enumOptions[i] ) == s )
        {
          idx = i;
          break;
        }
      }
      if ( idx >= 0 )
      {
        it.value() = idx;
      }
      else
      {
        // Also accept numeric-string fallback ("0","1",...) for robustness.
        bool ok = false;
        const int numericIdx = s.toInt( &ok );
        if ( ok && numericIdx >= 0 && numericIdx < static_cast<int>( port.enumOptions.size() ) )
          it.value() = numericIdx;
        else
          throw std::runtime_error( "Unknown enum value '" + s.toStdString() + "' for parameter " + port.name );
      }
    }
    else if ( port.isArray && it.value().typeId() == QMetaType::QStringList )
    {
      QStringList list = it.value().toStringList();
      QVariantList converted;
      converted.reserve( list.size() );
      for ( const QString &elem : list )
      {
        int idx = -1;
        for ( int i = 0; i < static_cast<int>( port.enumOptions.size() ); ++i )
          if ( QString::fromStdString( port.enumOptions[i] ) == elem ) { idx = i; break; }
        if ( idx >= 0 ) converted.append( idx );
        else throw std::runtime_error( "Unknown enum value '" + elem.toStdString() + "' for parameter " + port.name );
      }
      it.value() = converted;
    }
    else if ( port.isArray && it.value().typeId() == QMetaType::QVariantList )
    {
      QVariantList list = it.value().toList();
      for ( int i = 0; i < list.size(); ++i )
      {
        if ( list[i].typeId() == QMetaType::QString )
        {
          const QString s = list[i].toString();
          int idx = -1;
          for ( int j = 0; j < static_cast<int>( port.enumOptions.size() ); ++j )
            if ( QString::fromStdString( port.enumOptions[j] ) == s ) { idx = j; break; }
          if ( idx >= 0 ) list[i] = idx;
          else throw std::runtime_error( "Unknown enum value '" + s.toStdString() + "' for parameter " + port.name );
        }
      }
      it.value() = list;
    }
  }

  // External cancel bridge (e.g. JobEngine cancel flag): when the caller's
  // cancel is requested, propagate it into the feedback the algorithm observes
  // and abort promptly with a typed Cancelled error — a cancelled job must not
  // keep running to completion and writing its output.
  auto checkCancelled = [&]() {
    if ( isCancelledFn && isCancelledFn() )
    {
      feedback.cancel();
      throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                               "Processing algorithm cancelled: " + mDesc.id );
    }
  };

  // prepare() → runPrepared() → postProcess(true); on cancel/exception call
  // postProcess(false) to give the algorithm a chance to clean up partial work.
  try
  {
    if ( !algorithm->prepare( parameters, context, &feedback ) )
    {
      const QString err = feedback.textLog();
      throw std::runtime_error( err.isEmpty() ? "prepare() failed for " + mDesc.id
                                              : err.toStdString() );
    }
  }
  catch ( const QgsProcessingException &e )
  {
    throw std::runtime_error( e.what().toStdString() );
  }

  checkCancelled();
  if ( feedback.isCanceled() )
    throw std::runtime_error( "Processing algorithm cancelled before run: " + mDesc.id );

  std::atomic<bool> runDone{false};
  std::thread watcher;
  if ( isCancelledFn )
  {
    watcher = std::thread( [&runDone, isCancelledFn, &feedback]() {
      while ( !runDone.load( std::memory_order_relaxed ) )
      {
        if ( isCancelledFn() )
        {
          feedback.cancel();
          break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
      }
    } );
  }

  struct WatcherJoiner
  {
    std::atomic<bool> &done;
    std::thread &t;
    ~WatcherJoiner()
    {
      done.store( true, std::memory_order_relaxed );
      if ( t.joinable() )
        t.join();
    }
  } watcherJoiner{ runDone, watcher };

  QVariantMap runResults;
  try
  {
    runResults = algorithm->runPrepared( parameters, context, &feedback );
  }
  catch ( const QgsProcessingException &e )
  {
    try { algorithm->postProcess( context, &feedback, false ); } catch ( ... ) {}
    throw std::runtime_error( e.what().toStdString() );
  }

  checkCancelled();
  if ( feedback.isCanceled() )
  {
    try { algorithm->postProcess( context, &feedback, false ); } catch ( ... ) {}
    throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                             "Processing algorithm cancelled during run: " + mDesc.id );
  }

  QVariantMap results;
  try
  {
    results = algorithm->postProcess( context, &feedback, true );
  }
  catch ( const QgsProcessingException &e )
  {
    throw std::runtime_error( e.what().toStdString() );
  }
  if ( results.isEmpty() )
    results = runResults;

  Json::Value result( Json::objectValue );
  for ( auto it = results.constBegin(); it != results.constEnd(); ++it )
  {
    result[it.key().toStdString()] = variantToJsonValue( it.value() );
  }
  return result;
}

} // namespace sicnu::processing
