/***************************************************************************
 * processing_job_adapter.cpp
 ***************************************************************************/
#include "processing_job_adapter.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <qgsapplication.h>
#include <qgsprocessingalgorithm.h>
#include <qgsprocessingcontext.h>
#include <qgsexception.h>
#include <qgsprocessingfeedback.h>
#include <qgsprocessingregistry.h>
#include <qgsproject.h>

#include <memory>
#include <string>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::operators::ErrorCode;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

namespace ProcessingJobAdapter {

namespace {

constexpr const char *kProcessingPrefix = "processing:";

QVariant jsonValueToVariant( const Json::Value &v )
{
  if ( v.isNull() || v.isObject() || v.isArray() )
    return QVariant();
  if ( v.isBool() )
    return v.asBool();
  if ( v.isInt() || v.isUInt() )
    return static_cast<qint64>( v.asInt64() );
  if ( v.isDouble() )
    return v.asDouble();
  if ( v.isString() )
    return QString::fromStdString( v.asString() );
  return QVariant();
}

Json::Value variantToJson( const QVariant &v )
{
  switch ( v.userType() )
  {
    case QMetaType::Bool:
      return Json::Value( v.toBool() );
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
      return Json::Value( static_cast<Json::Int64>( v.toLongLong() ) );
    case QMetaType::Double:
    case QMetaType::Float:
      return Json::Value( v.toDouble() );
    case QMetaType::QString:
      return Json::Value( v.toString().toStdString() );
    default:
      if ( v.canConvert<QString>() )
        return Json::Value( v.toString().toStdString() );
      return Json::Value();
  }
}

/**
 * Prefix executor body: create a thread-local QgsProcessingContext so
 * prepare / runPrepared / postProcess all share the worker thread affinity.
 */
Json::Value runProcessingPrefixJob( const JobRequest &req, RSOperatorContext &ctx )
{
  std::string algId = req.algorithmId;
  const std::string prefix( kProcessingPrefix );
  if ( algId.rfind( prefix, 0 ) == 0 )
    algId = algId.substr( prefix.size() );

  if ( algId.empty() )
  {
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "processing: executor requires algorithm id after prefix" );
  }

  const QgsProcessingAlgorithm *prototype =
    QgsApplication::processingRegistry()->algorithmById( QString::fromStdString( algId ) );
  if ( !prototype )
  {
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "Unknown processing algorithm: " + algId );
  }

  std::unique_ptr<QgsProcessingAlgorithm> algorithm( prototype->create() );
  if ( !algorithm )
  {
    throw RSOperatorError( ErrorCode::QgisProcessingError,
                           "Failed to create algorithm: " + algId );
  }

  // Context affinity = this worker thread → prepare/run/postProcess all valid here.
  QgsProcessingContext context;
  if ( QgsProject *project = QgsProject::instance() )
  {
    context.setProject( project );
    context.setTransformContext( project->transformContext() );
  }

  QgsProcessingFeedback feedback;

  const QVariantMap parameters = jsonToVariantMap( req.params );
  ctx.logInfo( "Running processing algorithm " + algId );
  ctx.reportProgress( 0.0, "Preparing" );

  // Context was created on this worker thread → prepare / runPrepared /
  // postProcess all share the correct affinity (no main-thread requirement).
  try
  {
    if ( !algorithm->prepare( parameters, context, &feedback ) )
    {
      const QString err = feedback.textLog();
      throw RSOperatorError( ErrorCode::QgisProcessingError,
                             err.isEmpty() ? "prepare() failed for " + algId
                                           : err.toStdString() );
    }
  }
  catch ( const QgsProcessingException &e )
  {
    throw RSOperatorError( ErrorCode::QgisProcessingError, e.what().toStdString() );
  }

  ctx.throwIfCancelled();
  if ( feedback.isCanceled() )
    throw RSOperatorError( ErrorCode::Cancelled, "Processing algorithm cancelled" );

  ctx.reportProgress( 0.05, "Running" );
  QVariantMap runResults;
  try
  {
    runResults = algorithm->runPrepared( parameters, context, &feedback );
  }
  catch ( const QgsProcessingException &e )
  {
    try
    {
      algorithm->postProcess( context, &feedback, false );
    }
    catch ( ... )
    {
    }
    throw RSOperatorError( ErrorCode::QgisProcessingError, e.what().toStdString() );
  }

  if ( feedback.isCanceled() || ctx.isCancelled() )
  {
    try
    {
      algorithm->postProcess( context, &feedback, false );
    }
    catch ( ... )
    {
    }
    throw RSOperatorError( ErrorCode::Cancelled, "Processing algorithm cancelled" );
  }

  QVariantMap results;
  try
  {
    results = algorithm->postProcess( context, &feedback, true );
  }
  catch ( const QgsProcessingException &e )
  {
    throw RSOperatorError( ErrorCode::QgisProcessingError, e.what().toStdString() );
  }
  if ( results.isEmpty() )
    results = runResults;

  ctx.reportProgress( 1.0, "Succeeded" );

  Json::Value out = resultsToJson( results );
  if ( !out.isMember( "output" ) )
  {
    // Prefer first string result as generic "output" for the job panel loader.
    for ( auto it = results.constBegin(); it != results.constEnd(); ++it )
    {
      if ( it.value().userType() == QMetaType::QString )
      {
        const QString path = it.value().toString();
        if ( !path.isEmpty() )
        {
          out["output"] = path.toStdString();
          break;
        }
      }
    }
  }
  return out;
}

} // namespace

void registerProcessingJobExecutor()
{
  static bool registered = false;
  if ( registered )
    return;
  registered = true;

  JobEngine::instance().registerExecutor(
    kProcessingPrefix,
    []( const JobRequest &req, RSOperatorContext &ctx ) {
      return runProcessingPrefixJob( req, ctx );
    } );
}

Json::Value resultsToJson( const QVariantMap &results )
{
  Json::Value root( Json::objectValue );
  for ( auto it = results.constBegin(); it != results.constEnd(); ++it )
  {
    Json::Value j = variantToJson( it.value() );
    if ( !j.isNull() )
      root[it.key().toStdString()] = std::move( j );
  }
  return root;
}

QVariantMap jsonToVariantMap( const Json::Value &params )
{
  QVariantMap map;
  if ( !params.isObject() )
    return map;
  for ( const auto &name : params.getMemberNames() )
  {
    const QVariant v = jsonValueToVariant( params[name] );
    if ( v.isValid() )
      map.insert( QString::fromStdString( name ), v );
  }
  return map;
}

std::string processingAlgorithmId( const QString &qgisAlgorithmId )
{
  return std::string( kProcessingPrefix ) + qgisAlgorithmId.toStdString();
}

} // namespace ProcessingJobAdapter
