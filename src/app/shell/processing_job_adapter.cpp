/***************************************************************************
 * processing_job_adapter.cpp
 ***************************************************************************/
#include "processing_job_adapter.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/atomic_algorithm_adapter.h"

#include <QMetaType>

#include <string>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::operators::ErrorCode;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

namespace ProcessingJobAdapter {

namespace {

constexpr const char *kProcessingPrefix = "processing:";

/**
 * Prefix executor body (ADR 0062 sibling): strip the "processing:" prefix and
 * delegate to the AtomicAlgorithmRegistry adapter, which runs the full
 * prepare -> runPrepared -> postProcess lifecycle (ProviderAlgorithmAdapter).
 * This replaces the former inline duplicate of that lifecycle; the shared
 * path also powers the JobEngine fallback for provider algorithms.
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

  const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( algId );
  if ( !adapter )
  {
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "Unknown processing algorithm: " + algId );
  }

  ctx.logInfo( "Running processing algorithm " + algId );

  sicnu::processing::ProgressCallback progressBridge = [&ctx]( int percent, const std::string &message ) {
    ctx.reportProgress( percent / 100.0, message );
  };
  return adapter->execute( req.params, progressBridge );
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

std::string processingAlgorithmId( const QString &qgisAlgorithmId )
{
  return std::string( kProcessingPrefix ) + qgisAlgorithmId.toStdString();
}

} // namespace ProcessingJobAdapter
