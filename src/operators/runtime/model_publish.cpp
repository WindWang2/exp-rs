// src/operators/runtime/model_publish.cpp — the shared provenance publish
// authority for the model runtime lanes (see the header for the contract).
#include "operators/runtime/model_publish.h"

#include "operators/framework/rs_operator_error.h"
#include "runtime/observability/fault_point.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <ogr_spatialref.h>

#include <json/json.h>

#include <string>

namespace sicnu::operators::runtime {

std::string crsDisplayName( const QString &wkt )
{
  if ( wkt.trimmed().isEmpty() )
    return {};
  OGRSpatialReference srs;
  if ( srs.SetFromUserInput( wkt.toUtf8().constData() ) == OGRERR_NONE )
  {
    const char *authority = srs.GetAuthorityName( nullptr );
    const char *code = srs.GetAuthorityCode( nullptr );
    if ( authority && code )
      return std::string( authority ) + ":" + code;
  }
  const std::string text = wkt.toStdString();
  if ( text.size() <= 64 )
    return text;
  // Truncate on a safe boundary: never split a UTF-8 multibyte sequence.
  std::size_t cut = 61;
  while ( cut > 0 && ( text[cut] & 0xC0 ) == 0x80 )
    --cut;
  return text.substr( 0, cut ) + "...";
}

/// Shapefile companions for a vector output (empty for GPKG/GeoJSON) — the
/// same set the detection writer publishes and must roll back atomically.
QStringList detectionSidecars( const QString &main )
{
  const QFileInfo fi( main );
  if ( fi.suffix().toLower() != QLatin1String( "shp" ) )
    return {};
  const QString base = fi.path() + QLatin1Char( '/' ) + fi.completeBaseName();
  return { base + QStringLiteral( ".dbf" ), base + QStringLiteral( ".shx" ),
           base + QStringLiteral( ".prj" ), base + QStringLiteral( ".cpg" ) };
}

void removeWithSidecars( const QString &main )
{
  QFile::remove( main );
  for ( const QString &sidecar : detectionSidecars( main ) )
    QFile::remove( sidecar );
}

bool publishProvenanceSidecar( const QString &finalPath, const Json::Value &provenance,
                               const char *faultPoint, std::string *error )
{
  // Test-only fault injection (Verification Platform 8.0 pattern): routes
  // through the REAL failure branch so the publish rollback is provable.
  if ( faultPoint && SICNU_FAULT_POINT( faultPoint ) )
  {
    if ( error )
      *error = "failed to publish the provenance sidecar: fault-injected failure";
    return false;
  }
  const QString sidecarPath = finalPath + QStringLiteral( ".prov.json" );
  const QString stagePath = sidecarPath + QStringLiteral( ".stage~" );
  QFile stage( stagePath );
  if ( !stage.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
  {
    if ( error )
      *error = "failed to stage the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, provenance );
  const qint64 written = stage.write( text.data(), static_cast<qint64>( text.size() ) );
  stage.close();
  if ( stage.error() != QFileDevice::NoError || written != static_cast<qint64>( text.size() ) )
  {
    stage.remove();
    if ( error )
      *error = "failed to write the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  QFile::remove( sidecarPath ); // Windows rename does not overwrite
  if ( !QFile::rename( stagePath, sidecarPath ) )
  {
    stage.remove();
    if ( error )
      *error = "failed to publish the provenance sidecar: " + sidecarPath.toStdString();
    return false;
  }
  return true;
}

DetectionPublishGuard::DetectionPublishGuard( const QString &finalPath,
                                              const QString &backupSuffix )
    : m_final( finalPath ),
      m_backup( finalPath + backupSuffix ),
      m_backupSuffix( backupSuffix )
{
  m_hadExisting = QFile::exists( m_final );
  if ( !m_hadExisting )
    return;
  removeWithSidecars( m_backup );
  if ( !QFile::rename( m_final, m_backup ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "detection publish could not back up the previous product: "
                             + finalPath.toStdString() );
  // Sidecar / companion backup failures are never ignored — a failed backup
  // would leave the previous product unrestorable on rollback.
  for ( const QString &sidecar : detectionSidecars( m_final ) )
  {
    if ( !QFile::exists( sidecar ) )
      continue;
    if ( !QFile::rename( sidecar, sidecar + m_backupSuffix ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "detection publish could not back up sidecar: "
                               + sidecar.toStdString() );
  }
  if ( QFile::exists( m_final + QStringLiteral( ".prov.json" ) ) )
  {
    if ( !QFile::rename( m_final + QStringLiteral( ".prov.json" ),
                         m_backup + QStringLiteral( ".prov.json" ) ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "detection publish could not back up provenance sidecar: "
                               + finalPath.toStdString() );
  }
}

DetectionPublishGuard::~DetectionPublishGuard()
{
  if ( m_disarmed )
    return;
  removeWithSidecars( m_final );
  if ( !m_hadExisting )
    return;
  QFile::rename( m_backup, m_final );
  for ( const QString &sidecar : detectionSidecars( m_final ) )
  {
    const QString backupSidecar = sidecar + m_backupSuffix;
    if ( QFile::exists( backupSidecar ) )
      QFile::rename( backupSidecar, sidecar );
  }
  const QString backupProv = m_backup + QStringLiteral( ".prov.json" );
  if ( QFile::exists( backupProv ) )
    QFile::rename( backupProv, m_final + QStringLiteral( ".prov.json" ) );
}

void DetectionPublishGuard::disarm()
{
  m_disarmed = true;
  if ( m_hadExisting )
  {
    removeWithSidecars( m_backup );
    QFile::remove( m_backup + QStringLiteral( ".prov.json" ) );
  }
}

Json::Value buildDetectionProvenance( const ModelInfo &model, const ModelRuntimePtr &runtime,
                                      const DetectionTileStats &stats )
{
  Json::Value prov( Json::objectValue );
  prov["schema"] = "exp-rs-prov/1";

  Json::Value modelJson( Json::objectValue );
  modelJson["name"] = model.name;
  if ( !model.id.empty() )
    modelJson["id"] = model.id;
  if ( !model.modelVersion.empty() )
    modelJson["version"] = model.modelVersion;
  modelJson["identity_tag"] = model.identityTag();
  if ( !model.contentDigest.empty() )
    modelJson["content_digest"] = model.contentDigest;
  // The whole-package digest when the model ships one (Platform 9.0 M7/M8).
  if ( !model.packageDigest.empty() )
    modelJson["package_digest"] = model.packageDigest;
  modelJson["framework"] = model.framework;
  // Task intent travels with the identity (the scene artifact records the
  // same field): the sidecar names WHAT KIND of product this is.
  if ( !model.task.empty() )
    modelJson["task"] = model.task;
  if ( !model.sourceManifest.empty() )
    modelJson["source_manifest"] = model.sourceManifest;
  if ( !model.license.empty() )
    modelJson["license"] = model.license;
  prov["model"] = modelJson;

  Json::Value execution( Json::objectValue );
  if ( runtime )
  {
    execution["backend"] = runtime->backendName();
    execution["device"] = runtime->deviceName();
    // Platform 9.0 (M8) execution identity — honest-and-possibly-absent.
    const ProviderRuntimeDetails details = runtime->providerDetails();
    if ( !details.executionProvider.empty() )
      execution["execution_provider"] = details.executionProvider;
    if ( !details.runtimeVersion.empty() )
      execution["runtime_version"] = details.runtimeVersion;
  }
  execution["tile_size"] = stats.tileSize;
  execution["halo"] = stats.contextHalo;
  execution["batch_size"] = stats.batchSize;
  execution["tiles_planned"] = stats.tilesPlanned;
  execution["tiles_processed"] = stats.tilesProcessed;
  if ( stats.batchReductions > 0 )
    execution["batch_reductions"] = stats.batchReductions;
  prov["execution"] = execution;

  // Detection semantics: the effective thresholds (request overrides applied)
  // and the vocabulary are part of the output identity — a run at conf 0.25
  // and a run at conf 0.5 are different products.
  const ModelDetectionContract &det = model.output.detection;
  Json::Value detectionJson( Json::objectValue );
  detectionJson["conf_threshold"] = det.confThreshold;
  detectionJson["nms_iou"] = det.nmsIou;
  if ( !det.layout.empty() )
    detectionJson["layout"] = det.layout;
  detectionJson["raw_detections"] = stats.rawDetections;
  detectionJson["detections_kept"] = stats.detectionsKept;
  Json::Value classesJson( Json::arrayValue );
  for ( const std::string &cls : det.classes )
    classesJson.append( cls );
  detectionJson["classes"] = classesJson;
  prov["detection"] = detectionJson;

  // Input grid — the same block the raster lanes record (Platform 8.0).
  Json::Value inputs( Json::arrayValue );
  for ( const GridProvenance &grid : stats.inputGrids )
  {
    Json::Value input( Json::objectValue );
    input["name"] = grid.name;
    input["path"] = grid.path;
    if ( !grid.crs.empty() )
      input["crs"] = grid.crs;
    input["crs_verified"] = grid.crsVerified;
    input["width"] = grid.width;
    input["height"] = grid.height;
    if ( grid.frames > 1 )
      input["frames"] = grid.frames;
    if ( !grid.preprocessNote.empty() )
      input["preprocess"] = grid.preprocessNote;
    if ( grid.fingerprint.isObject() )
      input["fingerprint"] = grid.fingerprint;
    inputs.append( input );
  }
  prov["inputs"] = inputs;

  // A vector product: the output block describes features, not a grid — the
  // consumer-side verifier keys on format=="vector" and never GDALOpens it.
  Json::Value output( Json::objectValue );
  output["format"] = "vector";
  output["features"] = stats.detectionsKept;
  prov["output"] = output;

  prov["created_utc"] =
    QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ).toStdString();
  return prov;
}

} // namespace sicnu::operators::runtime
