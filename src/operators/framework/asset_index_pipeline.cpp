/***************************************************************************
 * asset_index_pipeline.cpp  —  Asset-driven spectral-index execution
 ***************************************************************************/
#include "asset_index_pipeline.h"

#include <QDateTime>
#include <QJsonObject>

#include <json/json.h>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/derivation_record.h"
#include "data/processing_asset_resolver.h"
#include "processing/framework/output_committer.h"

#include "rs_operator.h"
#include "rs_operator_context.h"
#include "rs_operator_error.h"
#include "rs_operator_registry.h"

using namespace sicnu::data;

namespace sicnu::operators
{

namespace
{

constexpr const char *kSpectralIndexAlgorithmId = "rs:spectral_index";

/// Builds the operator's JSON parameter map. The "input" is the resolved
/// asset's source location (the operator body opens it unchanged); "output"
/// is the temporary path the committer will publish from.
Json::Value buildOperatorParams( const SpectralIndexParams &params,
                                 const QString &resolvedInput,
                                 const QString &tempOutput )
{
  Json::Value json( Json::objectValue );
  json["input"] = resolvedInput.toStdString();
  json["output"] = tempOutput.toStdString();
  json["index"] = params.index.toStdString();
  json["nir"] = params.nir;
  json["red"] = params.red;
  json["green"] = params.green;
  json["blue"] = params.blue;
  json["swir"] = params.swir;
  return json;
}

/// Captures the parameter snapshot for the Derivation Record as JSON-native
/// values (lossless by construction).
QJsonObject paramsToJsonObject( const SpectralIndexParams &params )
{
  return QJsonObject{
    { QStringLiteral( "index" ), params.index },
    { QStringLiteral( "nir" ), params.nir },
    { QStringLiteral( "red" ), params.red },
    { QStringLiteral( "green" ), params.green },
    { QStringLiteral( "blue" ), params.blue },
    { QStringLiteral( "swir" ), params.swir },
  };
}

Diagnostic diagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

} // namespace

sicnu::CommitResult runSpectralIndexFromAsset( const AssetRef &input,
                                               const SpectralIndexParams &params,
                                               const StableOutputSpec &output,
                                               const ProcessingAssetResolver &resolver,
                                               sicnu::OutputCommitter &committer )
{
  // Resolve the AssetRef first. This validates existence, resolvable state,
  // the expected revision, and acquires a LeaseKind::Task lease held for the
  // life of `handle` — the input asset cannot be unloaded mid-run. A stale
  // expectedRevision is rejected here, before any work.
  Result<ResolvedAsset> resolved =
    resolver.resolve( input, QStringLiteral( "spectral_index" ),
                      AssetCapability::ReadablePixels );
  if ( !resolved )
    return sicnu::CommitResult::failure( resolved.diagnostics() );

  // Hold the Task lease (and the immutable snapshot) for the whole run. It is
  // released when `handle` leaves scope on any path below: success, operator
  // failure, or thrown exception. take() moves the lease out of the result so
  // it survives until scope exit.
  ResolvedAsset handle = resolved.take();
  const ResolvedAssetSnapshot &snapshot = handle.snapshot();

  // Run the operator synchronously against the resolved source location. The
  // operator body is unchanged: it opens the path with its GDAL wrapper.
  RSOperatorContext context;
  auto op = RSOperatorRegistry::instance().create( kSpectralIndexAlgorithmId );
  if ( !op )
  {
    committer.discardTemporary( output.tempPath );
    return sicnu::CommitResult::failure( diagnostic(
      QStringLiteral( "spectral_index.operator_missing" ),
      QStringLiteral( "The rs:spectral_index operator is not registered" ) ) );
  }

  const Json::Value opParams =
    buildOperatorParams( params, snapshot.sourceLocation(), output.tempPath );

  try
  {
    op->execute( opParams, context );
  }
  catch ( const RSOperatorError &e )
  {
    committer.discardTemporary( output.tempPath );
    return sicnu::CommitResult::failure( diagnostic(
      QStringLiteral( "spectral_index.run_failed" ),
      QString::fromStdString( e.message() ) ) );
  }
  catch ( const std::exception &e )
  {
    committer.discardTemporary( output.tempPath );
    return sicnu::CommitResult::failure( diagnostic(
      QStringLiteral( "spectral_index.run_failed" ),
      QString::fromLocal8Bit( e.what() ) ) );
  }

  // Success: commit the output transactionally. The committer validates,
  // atomically publishes, registers the Data Asset, and attaches the
  // Derivation Record. assetAdded fires once; display is opt-in.
  const QDateTime completedAt = QDateTime::currentDateTimeUtc();

  DerivationRecord derivation;
  derivation.algorithmId = QString::fromLatin1( kSpectralIndexAlgorithmId );
  derivation.algorithmVersion = QStringLiteral( "1.0" );
  derivation.parameters = paramsToJsonObject( params );
  DerivationInput inputRecord;
  inputRecord.assetId = snapshot.id();
  inputRecord.revision = snapshot.revision();
  derivation.inputs = { inputRecord };
  // Execution info: a run reference and the producing software version. There
  // is no Task Center task in the synchronous path, so taskReference carries a
  // run id derived from the completion timestamp.
  derivation.taskReference = completedAt.toString( QStringLiteral( "spectral_index-yyyyMMddHHmmsszzz" ) );
  derivation.softwareVersion = QStringLiteral( "SICNU GEO RS 1.0" );
  derivation.completedAtUtc = completedAt;

  AlgorithmOutputRequest commitRequest;
  commitRequest.kind = AssetKind::Raster;
  commitRequest.tempPath = output.tempPath;
  commitRequest.stablePath = output.stablePath;
  commitRequest.persistence = output.persistence;
  // Display is the caller's opt-in decision, surfaced via the committer's
  // displayRequested signal.
  commitRequest.autoLoad = output.autoLoad;
  commitRequest.derivation = derivation;

  return committer.commit( commitRequest );
}

} // namespace sicnu::operators
