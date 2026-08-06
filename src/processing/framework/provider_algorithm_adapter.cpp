// src/processing/framework/provider_algorithm_adapter.cpp
#include "provider_algorithm_adapter.h"
#include "json_params_converter.h"

#include "qgsprocessingalgorithm.h"
#include "qgsprocessingcontext.h"
#include "qgsprocessingfeedback.h"
#include "qgsprocessingoutputs.h"
#include "qgsprocessingparameters.h"
#include "qgsexception.h"
#include "qgsproject.h"
#include "qgis.h"

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

  // Agent metadata: auto-generate purpose from display name
  desc.agentMetadata.purpose = desc.description;
  const QStringList tags = alg.tags();
  for ( const QString &tag : tags )
    desc.agentMetadata.tags.push_back( tag.toStdString() );

  return desc;
}

// ---------------------------------------------------------------------------
// ProviderAlgorithmAdapter
// ---------------------------------------------------------------------------

ProviderAlgorithmAdapter::ProviderAlgorithmAdapter( const QgsProcessingAlgorithm &alg )
  : mAlg( &alg )
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

Json::Value ProviderAlgorithmAdapter::execute( const Json::Value &params, ProgressCallback progressCb )
{
  if ( !mAlg )
    throw std::runtime_error( "ProviderAlgorithmAdapter: null algorithm pointer" );

  // Create a fresh clone for this execution.
  std::unique_ptr<QgsProcessingAlgorithm> algorithm( mAlg->create() );
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
  if ( progressCb )
  {
    QObject::connect( &feedback, &QgsFeedback::progressChanged,
                      [progressCb]( double progress ) {
                        progressCb( static_cast<int>( progress ), "Processing..." );
                      } );
  }

  const QVariantMap parameters = jsonParamsToVariantMap( params );

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

  if ( feedback.isCanceled() )
    throw std::runtime_error( "Processing algorithm cancelled before run: " + mDesc.id );

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

  if ( feedback.isCanceled() )
  {
    try { algorithm->postProcess( context, &feedback, false ); } catch ( ... ) {}
    throw std::runtime_error( "Processing algorithm cancelled during run: " + mDesc.id );
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
