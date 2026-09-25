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

/// Shapefile companions for a vector output, optionally carrying a backup
/// suffix. The set is the vector writer's own publish family (dbf/shx/prj/
/// cpg plus the QGIS/shape index riders qpj/sbn/sbx/qix — a republish must
/// never leave a stale CRS override (.qpj) or index next to the new .prj).
/// The suffix is how the BACKUP family names them: companions of
/// "out.shp" with suffix ".det-prev~" are "out.dbf.det-prev~", ... (never
/// classify the backup path by suffix — "out.shp.det-prev~" is not a .shp).
QStringList detectionSidecarsFor( const QString &main, const QString &suffix )
{
  const QFileInfo fi( main );
  if ( fi.suffix().toLower() != QLatin1String( "shp" ) )
    return {};
  const QString base = fi.path() + QLatin1Char( '/' ) + fi.completeBaseName();
  QStringList out;
  for ( const char *ext : { ".dbf", ".shx", ".prj", ".cpg", ".qpj", ".sbn", ".sbx", ".qix" } )
    out << base + ext + suffix;
  return out;
}

void removeWithSidecars( const QString &main )
{
  QFile::remove( main );
  for ( const QString &sidecar : detectionSidecarsFor( main, QString() ) )
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

void DetectionPublishGuard::removeBackupFamily()
{
  QFile::remove( m_backup );
  for ( const QString &companion : detectionSidecarsFor( m_final, m_backupSuffix ) )
    QFile::remove( companion );
  QFile::remove( m_backup + QStringLiteral( ".prov.json" ) );
}

DetectionPublishGuard::DetectionPublishGuard( const QString &finalPath,
                                              const QString &backupSuffix )
    : m_final( finalPath ),
      m_backup( finalPath + backupSuffix ),
      m_backupSuffix( backupSuffix )
{
  // Crash-orphan recovery: a previous run that died between parking the
  // previous product and publishing the new one leaves the final path
  // ABSENT and the backup family present. Adopt the parked product back
  // first — the consumer must not keep staring at a missing product, and
  // this run then parks a consistent state (which disarm() then cleans).
  if ( !QFile::exists( m_final ) && QFile::exists( m_backup ) )
  {
    // Companions and prov restore FIRST, the main file LAST: a crash
    // mid-recovery leaves the main still parked, which is exactly the state
    // the next run's adoption branch re-enters through — every crash suffix
    // is recoverable (mirror of the park ladder).
    for ( const QString &companion : detectionSidecarsFor( m_final, QString() ) )
    {
      const QString backupCompanion = companion + m_backupSuffix;
      if ( QFile::exists( backupCompanion ) )
        QFile::rename( backupCompanion, companion );
    }
    const QString backupProv = m_backup + QStringLiteral( ".prov.json" );
    if ( QFile::exists( backupProv ) )
      QFile::rename( backupProv, m_final + QStringLiteral( ".prov.json" ) );
    if ( !QFile::rename( m_backup, m_final ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "detection publish could not recover the previously parked "
                               "product: " + finalPath.toStdString() );
  }

  m_hadExisting = QFile::exists( m_final );
  if ( !m_hadExisting )
  {
    // A genuine first publish: pre-clean stray backup litter from an
    // interrupted run so it can never resurface or wedge a later park
    // (Windows rename does not overwrite).
    removeBackupFamily();
    return;
  }

  // Park companions and the provenance sidecar FIRST, the main file LAST:
  // a throwing constructor never runs the destructor, so every park failure
  // rolls back its already-parked predecessors — the previous product stays
  // exactly as it was (never a half-parked, invisible one).
  removeBackupFamily();

  QStringList parkedCompanions;
  for ( const QString &companion : detectionSidecarsFor( m_final, QString() ) )
  {
    if ( !QFile::exists( companion ) )
      continue;
    if ( !QFile::rename( companion, companion + m_backupSuffix ) )
    {
      for ( const QString &parked : parkedCompanions )
        QFile::rename( parked + m_backupSuffix, parked );
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "detection publish could not back up sidecar: "
                               + companion.toStdString() );
    }
    parkedCompanions << companion;
  }
  const QString provPath = m_final + QStringLiteral( ".prov.json" );
  m_hadProv = QFile::exists( provPath );
  if ( m_hadProv && !QFile::rename( provPath, m_backup + QStringLiteral( ".prov.json" ) ) )
  {
    for ( const QString &parked : parkedCompanions )
      QFile::rename( parked + m_backupSuffix, parked );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "detection publish could not back up provenance sidecar: "
                             + finalPath.toStdString() );
  }
  // Main last: the least-likely park to fail, the shortest visibility
  // window for the product.
  if ( !QFile::rename( m_final, m_backup ) )
  {
    for ( const QString &parked : parkedCompanions )
      QFile::rename( parked + m_backupSuffix, parked );
    if ( m_hadProv )
      QFile::rename( m_backup + QStringLiteral( ".prov.json" ), provPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "detection publish could not back up the previous product: "
                             + finalPath.toStdString() );
  }
}

DetectionPublishGuard::~DetectionPublishGuard()
{
  if ( m_disarmed )
    return;
  // Remove the partial NEW product (main + companions + its prov).
  removeWithSidecars( m_final );
  QFile::remove( m_final + QStringLiteral( ".prov.json" ) );
  if ( !m_hadExisting )
    return;
  // Restore the parked previous product: companions + prov first, the main
  // file LAST — a crash mid-restore leaves the main parked, i.e. exactly
  // the state the next run's adoption branch re-enters through.
  for ( const QString &companion : detectionSidecarsFor( m_final, QString() ) )
  {
    const QString backupCompanion = companion + m_backupSuffix;
    if ( QFile::exists( backupCompanion ) )
      QFile::rename( backupCompanion, companion );
  }
  const QString backupProv = m_backup + QStringLiteral( ".prov.json" );
  if ( QFile::exists( backupProv ) )
    QFile::rename( backupProv, m_final + QStringLiteral( ".prov.json" ) );
  QFile::rename( m_backup, m_final );
}

void DetectionPublishGuard::disarm()
{
  m_disarmed = true;
  // Drop the whole backup family unconditionally (idempotent removes): an
  // adopted crash orphan must be cleaned exactly like a live backup.
  removeBackupFamily();
}

void ProductPublishGuard::removeBackupFamily()
{
  QFile::remove( m_backup );
  QFile::remove( m_backup + QStringLiteral( ".prov.json" ) );
}

ProductPublishGuard::ProductPublishGuard( const QString &finalPath, const QString &backupSuffix,
                                          const char *parkFaultPoint,
                                          const QString &stageForCleanup )
    : m_final( finalPath ),
      m_backup( finalPath + backupSuffix ),
      m_stageForCleanup( stageForCleanup )
{
  const QString provPath = m_final + QStringLiteral( ".prov.json" );
  const QString backupProv = m_backup + QStringLiteral( ".prov.json" );
  // Every throw path below must not leave the freshly written stage behind
  // (a throwing constructor never runs the destructor — same stage cleanup
  // the inline park ladders this guard replaced performed).
  const auto fail = [ & ]( const char *what ) {
    if ( !m_stageForCleanup.isEmpty() )
      QFile::remove( m_stageForCleanup );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           std::string( what ) + finalPath.toStdString() );
  };

  // Crash-orphan recovery (mirror of DetectionPublishGuard's adoption): the
  // two half-published states a killed run leaves are folded back to the
  // last PUBLISHED pair before anything else touches the path. Sidecar
  // restore always precedes the main restore so a crash mid-adoption leaves
  // the main parked — exactly the state the next run's adoption re-enters.
  const bool finalPresent = QFile::exists( m_final );
  const bool backupPresent = QFile::exists( m_backup );
  if ( !finalPresent && backupPresent )
  {
    // Window A: crash between the main park and the swap. Restore the parked
    // sidecar (when one was parked) and the product. A sidecar on the final
    // path with no parked counterpart is the interrupted-adoption re-entry
    // (restored by a run that died mid-adoption, before the main rename):
    // it IS the parked product's own sidecar, so it stays — leaving it pairs
    // the restored product with exactly what it was published with.
    if ( QFile::exists( backupProv ) )
      QFile::rename( backupProv, provPath );
    if ( !QFile::rename( m_backup, m_final ) )
      fail( "publish could not recover the previously parked product: " );
  }
  else if ( finalPresent && backupPresent && !QFile::exists( provPath )
            && QFile::exists( backupProv ) )
  {
    // Window B: crash between the swap and the sidecar publish — the file on
    // the final path never completed publication (no sidecar), while the
    // parked pair is the last complete one. Adopt the parked pair.
    QFile::remove( m_final );
    QFile::rename( backupProv, provPath );
    if ( !QFile::rename( m_backup, m_final ) )
      fail( "publish could not recover the previously parked product: " );
  }

  m_hadExisting = QFile::exists( m_final );
  if ( !m_hadExisting )
  {
    // A genuine first publish: pre-clean stray backup litter from an
    // interrupted run so it can never resurface or wedge a later park
    // (Windows rename does not overwrite).
    removeBackupFamily();
    return;
  }

  // Park the provenance sidecar FIRST, the main file LAST: a park failure
  // rolls back its already-parked predecessor, and the least-likely park to
  // fail (the main rename) opens the shortest visibility window.
  removeBackupFamily();
  m_hadProv = QFile::exists( provPath );
  if ( m_hadProv && !QFile::rename( provPath, backupProv ) )
    fail( "publish could not back up the previous provenance sidecar: " );
  const bool parkFailed = ( parkFaultPoint && SICNU_FAULT_POINT( parkFaultPoint ) )
                          || !QFile::rename( m_final, m_backup );
  if ( parkFailed )
  {
    if ( m_hadProv )
      QFile::rename( backupProv, provPath );
    fail( "publish could not back up the previous product: " );
  }
}

ProductPublishGuard::~ProductPublishGuard()
{
  if ( m_disarmed )
    return;
  // Remove the partial NEW product and its sidecar, then restore the parked
  // previous pair — sidecar first, the main file LAST: a crash mid-restore
  // leaves the main parked, i.e. exactly the state the next run's adoption
  // branch re-enters through.
  QFile::remove( m_final );
  QFile::remove( m_final + QStringLiteral( ".prov.json" ) );
  if ( !m_hadExisting )
    return;
  const QString provPath = m_final + QStringLiteral( ".prov.json" );
  const QString backupProv = m_backup + QStringLiteral( ".prov.json" );
  if ( m_hadProv && QFile::exists( backupProv ) )
    QFile::rename( backupProv, provPath );
  QFile::rename( m_backup, m_final );
}

void ProductPublishGuard::publishStaged( const QString &stagePath, const char *swapFaultPoint,
                                         const std::string &errorWhat )
{
  const bool swapFailed = ( swapFaultPoint && SICNU_FAULT_POINT( swapFaultPoint ) )
                          || !QFile::rename( stagePath, m_final );
  if ( swapFailed )
  {
    QFile::remove( stagePath );
    throw RSOperatorError( ErrorCode::FileNotWritable, errorWhat );
  }
}

void ProductPublishGuard::disarm()
{
  m_disarmed = true;
  // Drop the backup family unconditionally (idempotent removes): an adopted
  // crash orphan must be cleaned exactly like a live backup.
  removeBackupFamily();
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

  // Input grid — the same block the raster lanes record (Platform 8.0),
  // including the prepared-feed lineage (parity with buildProvenanceDocument).
  Json::Value inputs( Json::arrayValue );
  for ( const GridProvenance &grid : stats.inputGrids )
  {
    Json::Value input( Json::objectValue );
    input["name"] = grid.name;
    input["path"] = grid.path;
    if ( !grid.preparedFrom.empty() )
    {
      Json::Value prepared( Json::arrayValue );
      for ( const std::string &origin : grid.preparedFrom )
        prepared.append( origin );
      input["prepared_from"] = prepared;
    }
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
