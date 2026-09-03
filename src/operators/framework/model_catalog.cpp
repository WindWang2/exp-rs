// src/operators/framework/model_catalog.cpp
#include "model_catalog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace sicnu::operators {

namespace {

std::mutex &catalogMutex()
{
  static std::mutex mutex;
  return mutex;
}

QString normalizedChecksum( const std::string &declared )
{
  QString s = QString::fromStdString( declared ).trimmed();
  if ( s.startsWith( QStringLiteral( "sha256:" ), Qt::CaseInsensitive ) )
    s.remove( 0, 7 );
  s.remove( ' ' );
  return s.toLower();
}

/// SHA-256 hex digest of a file, streamed in 1 MiB chunks. Empty on I/O error.
QString sha256File( const QString &path )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return QString();
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  std::vector<char> buffer( 1024 * 1024 );
  while ( true )
  {
    const qint64 read = file.read( buffer.data(), static_cast<qint64>( buffer.size() ) );
    if ( read < 0 )
      return QString();
    if ( read == 0 )
      break;
    hash.addData( QByteArrayView( buffer.data(), static_cast<qsizetype>( read ) ) );
  }
  return QString::fromLatin1( hash.result().toHex() );
}

std::vector<std::string> parseStringArray( const QJsonObject &obj, const QString &key )
{
  std::vector<std::string> out;
  const QJsonValue v = obj.value( key );
  if ( v.isArray() )
  {
    for ( const auto &e : v.toArray() )
      out.push_back( e.toString().toStdString() );
  }
  else if ( v.isString() )
  {
    out.push_back( v.toString().toStdString() );
  }
  return out;
}

std::vector<double> parseDoubleArray( const QJsonObject &obj, const QString &key )
{
  std::vector<double> out;
  const QJsonValue v = obj.value( key );
  if ( v.isArray() )
  {
    for ( const auto &e : v.toArray() )
    {
      if ( e.isDouble() )
        out.push_back( e.toDouble() );
    }
  }
  else if ( v.isDouble() )
  {
    out.push_back( v.toDouble() );
  }
  return out;
}

void appendJsonArray( Json::Value &out, const char *key, const std::vector<std::string> &values )
{
  Json::Value arr( Json::arrayValue );
  for ( const auto &v : values )
    arr.append( v );
  out[key] = arr;
}

ModelInfo parseManifest( const QJsonObject &obj, const std::string &source )
{
  ModelInfo info;
  info.name = obj.value( QStringLiteral( "name" ) ).toString().toStdString();
  info.task = obj.value( QStringLiteral( "task" ) ).toString().toStdString();
  info.inputType = obj.value( QStringLiteral( "input" ) ).toString().toStdString();
  info.outputType = obj.value( QStringLiteral( "output" ) ).toString().toStdString();
  info.framework = obj.value( QStringLiteral( "framework" ) ).toString().toStdString();
  if ( info.framework.empty() )
    info.framework = "onnx";
  info.path = obj.value( QStringLiteral( "path" ) ).toString().toStdString();
  info.gpu = obj.value( QStringLiteral( "gpu" ) ).toBool( false );
  const QJsonValue accuracy = obj.value( QStringLiteral( "accuracy" ) );
  info.accuracy = accuracy.isDouble() ? accuracy.toDouble() : -1.0;
  info.description = obj.value( QStringLiteral( "description" ) ).toString().toStdString();
  info.tags = parseStringArray( obj, QStringLiteral( "tags" ) );
  info.sourceManifest = source;

  // --- Manifest v2: artifact ------------------------------------------------
  const QJsonObject artifactObj = obj.value( QStringLiteral( "artifact" ) ).toObject();
  info.artifact.path = artifactObj.value( QStringLiteral( "path" ) ).toString().toStdString();
  if ( info.artifact.path.empty() )
    info.artifact.path = info.path; // legacy top-level path
  info.artifact.checksum = artifactObj.value( QStringLiteral( "checksum" ) ).toString().toStdString();
  const QJsonValue artifactSize = artifactObj.value( QStringLiteral( "size_bytes" ) );
  if ( artifactSize.isDouble() && artifactSize.toDouble() >= 0.0 )
    info.artifact.sizeBytes = static_cast<unsigned long long>( artifactSize.toDouble() );

  // --- Manifest v2: input contract ------------------------------------------
  const QJsonValue inputVal = obj.value( QStringLiteral( "input" ) );
  const QJsonObject inputObj = inputVal.isObject() ? inputVal.toObject() : QJsonObject();
  if ( inputVal.isObject() )
  {
    info.input.dataType = inputObj.value( QStringLiteral( "data_type" ) ).toString().toStdString();
    info.input.dtype = inputObj.value( QStringLiteral( "dtype" ) ).toString().toStdString();
    info.input.layout = inputObj.value( QStringLiteral( "layout" ) ).toString().toStdString();
    if ( info.input.layout.empty() )
      info.input.layout = "NCHW";
    info.input.bandRoles = parseStringArray( inputObj, QStringLiteral( "band_roles" ) );
    const int inW = inputObj.value( QStringLiteral( "width" ) ).toInt( 0 );
    const int inH = inputObj.value( QStringLiteral( "height" ) ).toInt( 0 );
    info.input.width = inW > 0 ? inW : 0;
    info.input.height = inH > 0 ? inH : 0;
  }
  if ( info.inputType.empty() )
    info.inputType = info.input.dataType.empty() ? "raster" : info.input.dataType;
  // Legacy flat band_roles next to the manifest root.
  if ( info.input.bandRoles.empty() )
    info.input.bandRoles = parseStringArray( obj, QStringLiteral( "band_roles" ) );
  info.supportedBandRoles = info.input.bandRoles;

  // --- Manifest v2: output contract -----------------------------------------
  const QJsonValue outputVal = obj.value( QStringLiteral( "output" ) );
  const QJsonObject outputObj = outputVal.isObject() ? outputVal.toObject() : QJsonObject();
  if ( outputVal.isObject() )
  {
    info.output.type = outputObj.value( QStringLiteral( "type" ) ).toString().toStdString();
    info.output.tensorNames = parseStringArray( outputObj, QStringLiteral( "tensor_names" ) );
    info.output.classes = parseStringArray( outputObj, QStringLiteral( "classes" ) );
    info.output.threshold = outputObj.value( QStringLiteral( "threshold" ) ).toDouble( -1.0 );
  }
  if ( info.outputType.empty() )
    info.outputType = info.output.type;

  // --- Domain ---------------------------------------------------------------
  const QJsonObject domainObj = obj.value( QStringLiteral( "domain" ) ).toObject();
  info.sensors = domainObj.contains( QStringLiteral( "sensors" ) )
                   ? parseStringArray( domainObj, QStringLiteral( "sensors" ) )
                   : parseStringArray( obj, QStringLiteral( "sensors" ) );
  // Multimodal / temporal contract (goal §9): same domain-object-or-root
  // fallback vocabulary as sensors.
  info.modalities = domainObj.contains( QStringLiteral( "modalities" ) )
                      ? parseStringArray( domainObj, QStringLiteral( "modalities" ) )
                      : parseStringArray( obj, QStringLiteral( "modalities" ) );
  info.polarizations = domainObj.contains( QStringLiteral( "polarizations" ) )
                         ? parseStringArray( domainObj, QStringLiteral( "polarizations" ) )
                         : parseStringArray( obj, QStringLiteral( "polarizations" ) );
  info.temporalLength = domainObj.contains( QStringLiteral( "temporal_length" ) )
                          ? domainObj.value( QStringLiteral( "temporal_length" ) ).toInt()
                          : obj.value( QStringLiteral( "temporal_length" ) ).toInt();
  {
    const QString radiometric = domainObj.contains( QStringLiteral( "radiometric_state" ) )
                                  ? domainObj.value( QStringLiteral( "radiometric_state" ) ).toString()
                                  : obj.value( QStringLiteral( "radiometric_state" ) ).toString();
    info.radiometricState = radiometric.toStdString();
  }
  std::vector<double> resArr;
  if ( domainObj.contains( QStringLiteral( "resolution_range" ) ) )
    resArr = parseDoubleArray( domainObj, QStringLiteral( "resolution_range" ) );
  else
    resArr = parseDoubleArray( obj, QStringLiteral( "resolution_range" ) );
  if ( resArr.size() >= 2 )
  {
    info.minResolutionMeters = resArr.at( 0 );
    info.maxResolutionMeters = resArr.at( 1 );
  }

  // --- Manifest v2: preprocess ----------------------------------------------
  const QJsonObject preObj = obj.value( QStringLiteral( "preprocess" ) ).toObject();
  info.preprocess.normalize = preObj.value( QStringLiteral( "normalize" ) ).toString().toStdString();
  info.preprocess.mean = parseDoubleArray( preObj, QStringLiteral( "mean" ) );
  info.preprocess.stdv = parseDoubleArray( preObj, QStringLiteral( "std" ) );
  const double scale = preObj.value( QStringLiteral( "scale" ) ).toDouble( 1.0 );
  info.preprocess.scale = scale > 0.0 ? scale : 1.0;
  info.preprocess.resize = preObj.value( QStringLiteral( "resize" ) ).toString().toStdString();
  info.preprocess.interpolation = preObj.value( QStringLiteral( "interpolation" ) ).toString().toStdString();
  info.preprocess.nodataPolicy = preObj.value( QStringLiteral( "nodata_policy" ) ).toString().toStdString();

  // --- Manifest v2: tiling ---------------------------------------------------
  const QJsonObject tilingObj = obj.value( QStringLiteral( "tiling" ) ).toObject();
  if ( tilingObj.contains( QStringLiteral( "supported" ) ) )
    info.tiling.supported = tilingObj.value( QStringLiteral( "supported" ) ).toBool( true );
  const int tileSize = tilingObj.value( QStringLiteral( "tile_size" ) ).toInt( 0 );
  info.tiling.tileSize = tileSize > 0 ? tileSize : 0;
  const int overlap = tilingObj.value( QStringLiteral( "overlap" ) ).toInt( 0 );
  info.tiling.overlap = overlap > 0 ? overlap : 0;
  const int halo = tilingObj.value( QStringLiteral( "halo" ) ).toInt( 0 );
  info.tiling.halo = halo > 0 ? halo : 0;
  const int batch = tilingObj.value( QStringLiteral( "batch_size" ) ).toInt( 1 );
  info.tiling.batchSize = std::clamp( batch, 1, 64 );

  // --- Manifest v2: postprocess ----------------------------------------------
  const QJsonObject postObj = obj.value( QStringLiteral( "postprocess" ) ).toObject();
  info.postprocess.nms = postObj.value( QStringLiteral( "nms" ) ).toBool( false );
  info.postprocess.maskThreshold = postObj.value( QStringLiteral( "mask_threshold" ) ).toDouble( -1.0 );
  info.postprocess.polygonize = postObj.value( QStringLiteral( "polygonize" ) ).toBool( false );
  const double simplify = postObj.value( QStringLiteral( "simplify" ) ).toDouble( 0.0 );
  info.postprocess.simplify = simplify > 0.0 ? simplify : 0.0;

  // --- Runtime (v2 nested wins over legacy flat) ------------------------------
  const QJsonObject runtimeObj = obj.value( QStringLiteral( "runtime" ) ).toObject();
  info.runtime.gpu = runtimeObj.contains( QStringLiteral( "gpu" ) )
                       ? runtimeObj.value( QStringLiteral( "gpu" ) ).toBool( info.gpu )
                       : info.gpu;
  info.runtime.cpuFallback = runtimeObj.value( QStringLiteral( "cpu_fallback" ) ).toBool(
    obj.value( QStringLiteral( "cpu_fallback" ) ).toBool( true ) );
  info.runtime.estimatedRamMb = std::max( 0, runtimeObj.value( QStringLiteral( "estimated_ram_mb" ) ).toInt(
    obj.value( QStringLiteral( "estimated_ram_mb" ) ).toInt( 0 ) ) );
  info.runtime.estimatedVramMb = std::max( 0, runtimeObj.value( QStringLiteral( "estimated_vram_mb" ) ).toInt(
    obj.value( QStringLiteral( "estimated_vram_mb" ) ).toInt( 0 ) ) );

  // Tiling support flag: nested tiling.supported, legacy supports_tiling, in that order.
  info.supportsTiling = runtimeObj.contains( QStringLiteral( "supports_tiling" ) )
                          ? runtimeObj.value( QStringLiteral( "supports_tiling" ) ).toBool(
                              obj.value( QStringLiteral( "supports_tiling" ) ).toBool( true ) )
                          : info.tiling.supported;
  info.tiling.supported = info.supportsTiling;

  // Legacy flat mirrors (single parse point above keeps these consistent).
  info.gpu = info.runtime.gpu;
  info.cpuFallback = info.runtime.cpuFallback;
  info.estimatedVramMb = info.runtime.estimatedVramMb;
  info.path = info.artifact.path;

  // --- Contract sanity (InvalidManifest reasons) ------------------------------
  auto markInvalid = [&info]( std::string reason ) {
    info.readiness = ModelReadiness::InvalidManifest;
    // APPEND, don't replace: a manifest with several declared-but-unexecuted
    // knobs must report every one of them (the last-wins behavior hid all
    // but the final finding).
    if ( info.readinessReason.empty() )
      info.readinessReason = std::move( reason );
    else
      info.readinessReason += "; " + reason;
  };
  if ( !info.input.layout.empty() && info.input.layout != "NCHW" && info.input.layout != "nchw" )
    markInvalid( "unsupported input layout '" + info.input.layout + "' (only NCHW is executed)" );
  if ( info.preprocess.normalize == "mean_std"
       && info.preprocess.mean.empty() && info.preprocess.stdv.empty() )
    markInvalid( "preprocess.normalize is mean_std but neither mean nor std is declared" );
  const size_t channelCount = info.supportedBandRoles.size();
  if ( channelCount > 0 )
  {
    if ( !info.preprocess.mean.empty() && info.preprocess.mean.size() != channelCount )
      markInvalid( "preprocess.mean has " + std::to_string( info.preprocess.mean.size() )
                   + " entries but the model declares " + std::to_string( channelCount ) + " band roles" );
    if ( !info.preprocess.stdv.empty() && info.preprocess.stdv.size() != channelCount )
      markInvalid( "preprocess.std has " + std::to_string( info.preprocess.stdv.size() )
                   + " entries but the model declares " + std::to_string( channelCount ) + " band roles" );
  }
  if ( info.preprocess.resize == "to_input" && ( info.input.width <= 0 || info.input.height <= 0 ) )
    markInvalid( "preprocess.resize is to_input but input.width/height are not declared" );

  // Declared-but-unenforced values must fail loudly instead of silently
  // running identity behaviour (#646) - the read-but-never-enforced class
  // that #632 closed for input.dtype.
  if ( !info.preprocess.normalize.empty()
       && info.preprocess.normalize != "none"
       && info.preprocess.normalize != "linear"
       && info.preprocess.normalize != "mean_std" )
    markInvalid( "unsupported preprocess.normalize '" + info.preprocess.normalize
                 + "' (supported: none, linear, mean_std)" );
  if ( !info.preprocess.nodataPolicy.empty() && info.preprocess.nodataPolicy != "zero" )
    markInvalid( "unsupported preprocess.nodata_policy '" + info.preprocess.nodataPolicy
                 + "' (only 'zero' is executed)" );
  if ( info.tiling.tileSize > 0 )
  {
    if ( info.tiling.tileSize > 32768 )
      markInvalid( "tiling.tile_size " + std::to_string( info.tiling.tileSize )
                   + " is absurdly large (max 32768)" );
    if ( info.tiling.halo > info.tiling.tileSize / 2 )
      markInvalid( "tiling.halo " + std::to_string( info.tiling.halo )
                   + " exceeds tile_size/2 - the inference window would be memory-unbounded" );
    if ( info.tiling.overlap > info.tiling.tileSize / 2 )
      markInvalid( "tiling.overlap " + std::to_string( info.tiling.overlap )
                   + " exceeds tile_size/2" );
  }
  else if ( info.tiling.halo > 0 || info.tiling.overlap > 0 )
  {
    markInvalid( "tiling.halo/overlap declared but tiling.tile_size is not" );
  }
  if ( info.preprocess.scale != 1.0
       && info.preprocess.normalize != "linear" && info.preprocess.normalize != "mean_std" )
    markInvalid( "preprocess.scale is declared but normalize is neither linear nor mean_std - "
                 "the scale would silently not execute; set normalize or remove the scale" );
  if ( info.postprocess.nms )
    markInvalid( "postprocess.nms is declared but not implemented by any runtime - remove it or implement NMS" );
  if ( info.postprocess.polygonize )
    markInvalid( "postprocess.polygonize is declared but not implemented by any runtime - remove it or implement mask->polygon chaining" );
  if ( info.postprocess.simplify > 0.0 )
    markInvalid( "postprocess.simplify is declared but not implemented by any runtime" );
  if ( info.output.threshold >= 0.0 )
    markInvalid( "output.threshold is declared but not executed (use postprocess.mask_threshold, which the runtime enforces)" );

  // --- Artifact path resolution (manifest-dir relative — never CWD) -----------
  if ( !info.artifact.path.empty() )
  {
    const QString written = QString::fromStdString( info.artifact.path );
    QFileInfo artifactInfo( written );
    if ( artifactInfo.isRelative() )
      artifactInfo = QFileInfo( QFileInfo( QString::fromStdString( source ) ).absoluteDir(), written );
    info.resolvedArtifactPath = artifactInfo.absoluteFilePath().toStdString();
  }

  return info;
}

} // namespace

Json::Value ModelInfo::toJson() const
{
  Json::Value out( Json::objectValue );
  out["name"] = name;
  out["task"] = task;
  out["input"] = inputType;
  out["output"] = outputType;
  out["framework"] = framework;
  if ( !path.empty() )
    out["path"] = path;
  out["gpu"] = gpu;
  if ( accuracy >= 0.0 )
    out["accuracy"] = accuracy;
  if ( !description.empty() )
    out["description"] = description;
  if ( !tags.empty() )
    appendJsonArray( out, "tags", tags );
  if ( !sensors.empty() )
    appendJsonArray( out, "sensors", sensors );
  if ( !supportedBandRoles.empty() )
    appendJsonArray( out, "band_roles", supportedBandRoles );
  if ( !modalities.empty() )
    appendJsonArray( out, "modalities", modalities );
  if ( !polarizations.empty() )
    appendJsonArray( out, "polarizations", polarizations );
  if ( temporalLength > 0 )
    out["temporal_length"] = temporalLength;
  if ( !radiometricState.empty() )
    out["radiometric_state"] = radiometricState;
  if ( minResolutionMeters >= 0.0 || maxResolutionMeters >= 0.0 )
  {
    Json::Value resRange( Json::arrayValue );
    resRange.append( minResolutionMeters );
    resRange.append( maxResolutionMeters );
    out["resolution_range"] = resRange;
  }
  Json::Value runtimeJson( Json::objectValue );
  runtimeJson["gpu"] = runtime.gpu;
  runtimeJson["estimated_vram_mb"] = runtime.estimatedVramMb;
  runtimeJson["supports_tiling"] = supportsTiling;
  runtimeJson["cpu_fallback"] = runtime.cpuFallback;
  runtimeJson["estimated_ram_mb"] = runtime.estimatedRamMb;
  out["runtime"] = runtimeJson;

  // Manifest v2 surface (additive; PART B consumers ignore unknown keys).
  out["readiness"] = modelReadinessName( readiness );
  if ( !readinessReason.empty() )
    out["readiness_reason"] = readinessReason;
  if ( !resolvedArtifactPath.empty() )
    out["resolved_artifact_path"] = resolvedArtifactPath;
  if ( !artifact.path.empty() || !artifact.checksum.empty() || artifact.sizeBytes > 0 )
  {
    Json::Value artifactJson( Json::objectValue );
    if ( !artifact.path.empty() )
      artifactJson["path"] = artifact.path;
    if ( !artifact.checksum.empty() )
      artifactJson["checksum"] = artifact.checksum;
    if ( artifact.sizeBytes > 0 )
      artifactJson["size_bytes"] = Json::Value::UInt64( artifact.sizeBytes );
    out["artifact"] = artifactJson;
  }
  if ( input.width > 0 || input.height > 0 || !input.dtype.empty()
       || !input.bandRoles.empty() || !input.layout.empty() )
  {
    Json::Value inputJson( Json::objectValue );
    if ( !input.dataType.empty() )
      inputJson["data_type"] = input.dataType;
    if ( !input.dtype.empty() )
      inputJson["dtype"] = input.dtype;
    if ( !input.layout.empty() )
      inputJson["layout"] = input.layout;
    if ( !input.bandRoles.empty() )
      appendJsonArray( inputJson, "band_roles", input.bandRoles );
    if ( input.width > 0 )
      inputJson["width"] = input.width;
    if ( input.height > 0 )
      inputJson["height"] = input.height;
    out["input_contract"] = inputJson;
  }
  if ( !preprocess.normalize.empty() || !preprocess.mean.empty() || !preprocess.stdv.empty()
       || preprocess.scale != 1.0 || !preprocess.resize.empty() )
  {
    Json::Value pre( Json::objectValue );
    if ( !preprocess.normalize.empty() )
      pre["normalize"] = preprocess.normalize;
    if ( !preprocess.mean.empty() )
    {
      Json::Value arr( Json::arrayValue );
      for ( double v : preprocess.mean )
        arr.append( v );
      pre["mean"] = arr;
    }
    if ( !preprocess.stdv.empty() )
    {
      Json::Value arr( Json::arrayValue );
      for ( double v : preprocess.stdv )
        arr.append( v );
      pre["std"] = arr;
    }
    if ( preprocess.scale != 1.0 )
      pre["scale"] = preprocess.scale;
    if ( !preprocess.resize.empty() )
      pre["resize"] = preprocess.resize;
    if ( !preprocess.interpolation.empty() )
      pre["interpolation"] = preprocess.interpolation;
    if ( !preprocess.nodataPolicy.empty() )
      pre["nodata_policy"] = preprocess.nodataPolicy;
    out["preprocess"] = pre;
  }
  if ( tiling.tileSize > 0 || tiling.overlap > 0 || tiling.halo > 0 || tiling.batchSize != 1 )
  {
    Json::Value t( Json::objectValue );
    t["supported"] = tiling.supported;
    if ( tiling.tileSize > 0 )
      t["tile_size"] = tiling.tileSize;
    if ( tiling.overlap > 0 )
      t["overlap"] = tiling.overlap;
    if ( tiling.halo > 0 )
      t["halo"] = tiling.halo;
    if ( tiling.batchSize != 1 )
      t["batch_size"] = tiling.batchSize;
    out["tiling"] = t;
  }
  if ( !output.tensorNames.empty() || !output.classes.empty() || output.threshold >= 0.0 )
  {
    Json::Value o( Json::objectValue );
    if ( !output.type.empty() )
      o["type"] = output.type;
    if ( !output.tensorNames.empty() )
      appendJsonArray( o, "tensor_names", output.tensorNames );
    if ( !output.classes.empty() )
      appendJsonArray( o, "classes", output.classes );
    if ( output.threshold >= 0.0 )
      o["threshold"] = output.threshold;
    out["output_contract"] = o;
  }
  if ( postprocess.nms || postprocess.maskThreshold >= 0.0 || postprocess.polygonize
       || postprocess.simplify > 0.0 )
  {
    Json::Value p( Json::objectValue );
    if ( postprocess.nms )
      p["nms"] = true;
    if ( postprocess.maskThreshold >= 0.0 )
      p["mask_threshold"] = postprocess.maskThreshold;
    if ( postprocess.polygonize )
      p["polygonize"] = true;
    if ( postprocess.simplify > 0.0 )
      p["simplify"] = postprocess.simplify;
    out["postprocess"] = p;
  }

  out["sourceManifest"] = sourceManifest;
  return out;
}

Json::Value ModelCandidate::toJson() const
{
  Json::Value out( Json::objectValue );
  out["model"] = model.toJson();
  out["score"] = score;
  out["compatible"] = compatible;
  Json::Value matches( Json::arrayValue );
  for ( const auto &r : matchReasons )
    matches.append( r );
  out["match_reasons"] = matches;
  Json::Value incompat( Json::arrayValue );
  for ( const auto &r : incompatibilityReasons )
    incompat.append( r );
  out["incompatibility_reasons"] = incompat;
  return out;
}

/// Checksum cache entry: an artifact verified during an earlier load.
struct ModelCatalog::VerifiedArtifact
{
  QString path;
  unsigned long long sizeBytes = 0;
  qint64 mtimeMs = 0;
  QString checksumHex;
};

ModelCatalog &ModelCatalog::instance()
{
  static ModelCatalog catalog;
  return catalog;
}

std::string ModelCatalog::defaultModelsDirectory()
{
  const QString envDir = QProcessEnvironment::systemEnvironment().value(
    QStringLiteral( "SICNU_MODELS_DIR" ) );
  if ( !envDir.isEmpty() )
    return envDir.toStdString();

  const QDir cwdModels( QDir::current().filePath( QStringLiteral( "models" ) ) );
  if ( cwdModels.exists() )
    return cwdModels.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appModels( QCoreApplication::applicationDirPath()
                          + QStringLiteral( "/../models" ) );
    if ( appModels.exists() )
      return appModels.absolutePath().toStdString();
  }

  return QDir::current().filePath( QStringLiteral( "models" ) ).toStdString();
}

void ModelCatalog::setDirectory( const std::string &dir )
{
  {
    std::lock_guard<std::mutex> lock( catalogMutex() );
    mDirectory = dir;
    mLoaded = false;
  }
  reload();
}

std::string ModelCatalog::directory() const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  return mDirectory.empty() ? defaultModelsDirectory() : mDirectory;
}

bool ModelCatalog::verifyArtifactLocked( ModelInfo &info ) const
{
  auto fail = [&info]( ModelReadiness state, std::string reason ) {
    info.readiness = state;
    info.readinessReason = std::move( reason );
    return false;
  };
  if ( info.artifact.path.empty() )
    return fail( ModelReadiness::MissingArtifact,
                 "manifest declares no artifact path (template manifest — download weights and set artifact.path)" );
  const QString resolved = QString::fromStdString( info.resolvedArtifactPath );
  QFileInfo artifactInfo( resolved );
  if ( !artifactInfo.exists() || !artifactInfo.isFile() )
    return fail( ModelReadiness::MissingArtifact, "artifact not found: " + info.resolvedArtifactPath );
  if ( info.artifact.sizeBytes > 0
       && static_cast<unsigned long long>( artifactInfo.size() ) != info.artifact.sizeBytes )
    return fail( ModelReadiness::ChecksumMismatch,
                 "artifact size mismatch: manifest declares " + std::to_string( info.artifact.sizeBytes )
                   + " bytes, file has "
                   + std::to_string( static_cast<unsigned long long>( artifactInfo.size() ) ) );
  if ( info.artifact.checksum.empty() )
    return true; // present, no digest declared → trust it

  const QString expected = normalizedChecksum( info.artifact.checksum );
  if ( expected.size() != 64 )
    return fail( ModelReadiness::InvalidManifest,
                 "artifact checksum is not a valid SHA-256 hex digest" );
  const qint64 mtimeMs = artifactInfo.lastModified().toMSecsSinceEpoch();
  const unsigned long long sizeBytes = static_cast<unsigned long long>( artifactInfo.size() );
  QString actual;
  for ( const auto &verified : mVerified )
  {
    if ( verified.path == resolved && verified.sizeBytes == sizeBytes && verified.mtimeMs == mtimeMs )
    {
      actual = verified.checksumHex;
      break;
    }
  }
  if ( actual.isEmpty() )
  {
    actual = sha256File( resolved );
    if ( actual.isEmpty() )
      return fail( ModelReadiness::ChecksumMismatch,
                   "artifact unreadable while verifying checksum: " + info.resolvedArtifactPath );
    if ( mVerified.size() > 64 )
      mVerified.clear(); // bounded cache; re-hashing is only a cost, never a correctness issue
    mVerified.push_back( VerifiedArtifact{ resolved, sizeBytes, mtimeMs, actual } );
  }
  if ( actual != expected )
    return fail( ModelReadiness::ChecksumMismatch,
                 "artifact checksum mismatch: expected " + expected.toStdString()
                   + ", computed " + actual.toStdString() );
  return true;
}

void ModelCatalog::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;

  mModels.clear();
  mIssues.clear();
  const QDir dir( QString::fromStdString( mDirectory.empty() ? defaultModelsDirectory() : mDirectory ) );
  if ( dir.exists() )
  {
    std::vector<std::string> seenNames;
    const auto entries = dir.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );
    for ( const auto &entry : entries )
    {
      const QString manifestPath = QDir( entry.absoluteFilePath() ).filePath( QStringLiteral( "model.json" ) );
      QFile file( manifestPath );
      if ( !file.open( QIODevice::ReadOnly ) )
      {
        mIssues.push_back( { manifestPath.toStdString(), "manifest not readable" } );
        continue;
      }

      QJsonParseError parseError{};
      const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
      if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
      {
        mIssues.push_back( { manifestPath.toStdString(),
                             "manifest is not valid JSON: "
                               + parseError.errorString().toStdString() } );
        continue;
      }

      ModelInfo info = parseManifest( doc.object(), manifestPath.toStdString() );
      if ( info.name.empty() )
      {
        mIssues.push_back( { manifestPath.toStdString(), "manifest has no 'name'" } );
        continue;
      }
      if ( std::find( seenNames.begin(), seenNames.end(), info.name ) != seenNames.end() )
      {
        mIssues.push_back( { manifestPath.toStdString(),
                             "duplicate model name '" + info.name + "' (first manifest wins)" } );
        continue;
      }
      seenNames.push_back( info.name );

      // Catalog-static readiness: contract errors parsed above already set
      // InvalidManifest; otherwise verify the artifact itself (which sets
      // MissingArtifact / ChecksumMismatch / InvalidManifest on failure).
      if ( info.readiness == ModelReadiness::InvalidManifest )
      {
        // reason kept from parseManifest
      }
      else if ( verifyArtifactLocked( info ) )
      {
        info.readiness = ModelReadiness::Ready;
        info.readinessReason.clear();
      }
      mModels.push_back( std::move( info ) );
    }
  }
  mLoaded = true;
}

void ModelCatalog::reload()
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  mLoaded = false;
  ensureLoadedLocked();
}

std::vector<ModelInfo> ModelCatalog::models() const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  ensureLoadedLocked();
  return mModels;
}

std::vector<ModelInfo> ModelCatalog::modelsByTask( const std::string &task ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  ensureLoadedLocked();
  std::vector<ModelInfo> result;
  for ( const auto &model : mModels )
  {
    if ( model.task == task )
      result.push_back( model );
  }
  return result;
}

std::optional<ModelInfo> ModelCatalog::find( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  ensureLoadedLocked();
  for ( const auto &model : mModels )
  {
    if ( model.name == name )
      return model;
  }
  return std::nullopt;
}

std::vector<ModelCatalogIssue> ModelCatalog::issues() const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  ensureLoadedLocked();
  return mIssues;
}

std::vector<ModelCandidate> ModelCatalog::rankModels( const ModelQueryCriteria &criteria ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  ensureLoadedLocked();

  std::vector<ModelCandidate> candidates;
  candidates.reserve( mModels.size() );

  for ( const auto &model : mModels )
  {
    ModelCandidate cand;
    cand.model = model;
    cand.score = 0.5; // base baseline score
    cand.compatible = true;

    // 0. Availability is a hard gate: a model whose artifact is missing or
    // whose manifest is broken cannot be selected however well it scores.
    if ( model.readiness != ModelReadiness::Ready )
    {
      cand.compatible = false;
      cand.score -= 0.4;
      cand.incompatibilityReasons.push_back(
        std::string( "Model not ready (" ) + modelReadinessName( model.readiness ) + "): "
        + ( model.readinessReason.empty() ? std::string( "no explanation recorded" )
                                          : model.readinessReason ) );
    }

    // 1. Task compatibility (hard requirement if requested)
    if ( !criteria.task.empty() )
    {
      if ( model.task == criteria.task )
      {
        cand.score += 0.3;
        cand.matchReasons.push_back( "Exact task match: " + criteria.task );
      }
      else
      {
        cand.compatible = false;
        cand.score -= 0.4;
        cand.incompatibilityReasons.push_back( "Task mismatch (expected: " + criteria.task + ", model: " + model.task + ")" );
      }
    }

    // 2. Sensor domain compatibility
    if ( !criteria.sensor.empty() )
    {
      if ( model.sensors.empty() )
      {
        cand.score += 0.05;
        cand.matchReasons.push_back( "Sensor-agnostic model" );
      }
      else
      {
        bool sensorMatch = false;
        for ( const auto &s : model.sensors )
        {
          if ( s == criteria.sensor )
          {
            sensorMatch = true;
            break;
          }
        }
        if ( sensorMatch )
        {
          cand.score += 0.15;
          cand.matchReasons.push_back( "Trained on sensor: " + criteria.sensor );
        }
        else
        {
          cand.score -= 0.1;
          cand.incompatibilityReasons.push_back( "Sensor not listed in model training domain" );
        }
      }
    }

    // 3. Band-role compatibility (hard gate when both sides declare roles)
    if ( !criteria.bandRoles.empty() )
    {
      if ( model.supportedBandRoles.empty() )
      {
        cand.score += 0.02;
        cand.matchReasons.push_back( "Model band roles unspecified (assumed compatible)" );
      }
      else
      {
        size_t covered = 0;
        for ( const auto &role : criteria.bandRoles )
        {
          if ( std::find( model.supportedBandRoles.begin(), model.supportedBandRoles.end(), role )
               != model.supportedBandRoles.end() )
            ++covered;
        }
        if ( covered == 0 )
        {
          cand.compatible = false;
          cand.score -= 0.3;
          cand.incompatibilityReasons.push_back(
            "None of the requested band roles are supported by the model (requested "
            + std::to_string( criteria.bandRoles.size() ) + " roles, model expects "
            + std::to_string( model.supportedBandRoles.size() ) + ")" );
        }
        else if ( covered < criteria.bandRoles.size() )
        {
          cand.compatible = false;
          cand.score -= 0.15;
          cand.incompatibilityReasons.push_back(
            "Band roles partially covered: " + std::to_string( covered ) + "/"
            + std::to_string( criteria.bandRoles.size() )
            + " requested roles supported by the model" );
        }
        else
        {
          cand.score += 0.15;
          cand.matchReasons.push_back( "Band roles cover the requested bands" );
        }
      }
    }

    // 4. Resolution range check
    if ( criteria.resolutionMeters > 0.0 )
    {
      if ( model.minResolutionMeters > 0.0 && criteria.resolutionMeters < model.minResolutionMeters * 0.5 )
      {
        cand.score -= 0.1;
        cand.incompatibilityReasons.push_back( "Spatial resolution finer than model minimum recommended" );
      }
      else if ( model.maxResolutionMeters > 0.0 && criteria.resolutionMeters > model.maxResolutionMeters * 2.0 )
      {
        cand.score -= 0.1;
        cand.incompatibilityReasons.push_back( "Spatial resolution coarser than model maximum recommended" );
      }
      else if ( model.minResolutionMeters > 0.0 || model.maxResolutionMeters > 0.0 )
      {
        cand.score += 0.1;
        cand.matchReasons.push_back( "Spatial resolution fits model design range" );
      }
    }

    // 5. Hardware and GPU availability
    if ( model.gpu )
    {
      if ( criteria.gpuAvailable )
      {
        cand.score += 0.1;
        cand.matchReasons.push_back( "GPU accelerated runtime available" );
        if ( criteria.maxVramMb > 0 && model.estimatedVramMb > criteria.maxVramMb )
        {
          if ( model.cpuFallback )
          {
            cand.score -= 0.05;
            cand.incompatibilityReasons.push_back( "VRAM budget exceeded; fallback to CPU execution" );
          }
          else
          {
            cand.compatible = false;
            cand.incompatibilityReasons.push_back( "Required VRAM exceeds available budget and CPU fallback is disabled" );
          }
        }
      }
      else
      {
        if ( model.cpuFallback )
        {
          cand.score -= 0.05;
          cand.matchReasons.push_back( "CPU fallback enabled (GPU not available)" );
        }
        else
        {
          cand.compatible = false;
          cand.incompatibilityReasons.push_back( "GPU required but not available on host" );
        }
      }
    }
    else
    {
      cand.matchReasons.push_back( "CPU lightweight runtime" );
    }

    // 6. Benchmark accuracy factor
    if ( model.accuracy >= 0.0 )
    {
      cand.score += model.accuracy * 0.1;
      cand.matchReasons.push_back( "Reported benchmark accuracy: " + std::to_string( model.accuracy ) );
    }

    cand.score = std::clamp( cand.score, 0.0, 1.0 );
    candidates.push_back( std::move( cand ) );
  }

  // Sort candidates: compatible models first, then descending by composite score
  std::sort( candidates.begin(), candidates.end(), []( const ModelCandidate &a, const ModelCandidate &b ) {
    if ( a.compatible != b.compatible )
      return a.compatible > b.compatible;
    if ( a.score != b.score )
      return a.score > b.score;
    // Deterministic tertiary key (#646): equal scores must not depend on
    // unordered_map iteration order (std::sort is not stable).
    return a.model.name < b.model.name;
  } );

  return candidates;
}

std::optional<std::string> ModelCatalog::resolveArtifactPath( const std::string &modelReference,
                                                              std::string *error )
{
  if ( modelReference.empty() )
  {
    if ( error )
      *error = "empty model reference";
    return std::nullopt;
  }
  const QFileInfo direct( QString::fromStdString( modelReference ) );
  if ( direct.exists() && direct.isFile() )
    return direct.absoluteFilePath().toStdString();

  const auto model = ModelCatalog::instance().find( modelReference );
  if ( !model )
  {
    if ( error )
      *error = "model reference is neither an existing file nor a catalog name: " + modelReference;
    return std::nullopt;
  }
  if ( model->readiness != ModelReadiness::Ready )
  {
    if ( error )
      *error = "model '" + model->name + "' is not ready ("
               + modelReadinessName( model->readiness ) + "): "
               + ( model->readinessReason.empty() ? std::string( "unavailable" )
                                                  : model->readinessReason );
    return std::nullopt;
  }
  return model->resolvedArtifactPath;
}

} // namespace sicnu::operators
