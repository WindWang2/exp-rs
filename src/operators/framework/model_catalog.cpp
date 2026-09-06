// src/operators/framework/model_catalog.cpp
#include "model_catalog.h"

#include "artifact_digest.h"

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

/// Parses one input object: the manifest v2 `input` section or, since v3,
/// one entry of the `inputs` array. Same keys for both: name, data_type,
/// dtype, layout, band_roles, width, height, temporal_length, temporal_collapse.
ModelInputContract parseModelInputContract( const QJsonObject &inputObj )
{
  ModelInputContract input;
  input.name = inputObj.value( QStringLiteral( "name" ) ).toString().toStdString();
  input.dataType = inputObj.value( QStringLiteral( "data_type" ) ).toString().toStdString();
  input.dtype = inputObj.value( QStringLiteral( "dtype" ) ).toString().toStdString();
  input.layout = inputObj.value( QStringLiteral( "layout" ) ).toString().toStdString();
  if ( input.layout.empty() )
    input.layout = "NCHW";
  input.bandRoles = parseStringArray( inputObj, QStringLiteral( "band_roles" ) );
  const int inW = inputObj.value( QStringLiteral( "width" ) ).toInt( 0 );
  const int inH = inputObj.value( QStringLiteral( "height" ) ).toInt( 0 );
  input.width = inW > 0 ? inW : 0;
  input.height = inH > 0 ? inH : 0;
  input.temporalLength = inputObj.value( QStringLiteral( "temporal_length" ) ).toInt( 0 );
  input.temporalCollapse = inputObj.value( QStringLiteral( "temporal_collapse" ) ).toString().toStdString();
  if ( input.temporalCollapse.empty() )
    input.temporalCollapse = "channels"; // documented default
  return input;
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

  // --- Platform 4.0 identity -------------------------------------------------
  info.id = obj.value( QStringLiteral( "id" ) ).toString().toStdString();
  info.modelVersion = obj.value( QStringLiteral( "model_version" ) ).toString().toStdString();
  info.license = obj.value( QStringLiteral( "license" ) ).toString().toStdString();
  info.source = obj.value( QStringLiteral( "source" ) ).toString().toStdString();

  // --- Manifest v2: artifact ------------------------------------------------
  const QJsonObject artifactObj = obj.value( QStringLiteral( "artifact" ) ).toObject();
  info.artifact.path = artifactObj.value( QStringLiteral( "path" ) ).toString().toStdString();
  if ( info.artifact.path.empty() )
    info.artifact.path = info.path; // legacy top-level path
  info.artifact.checksum = artifactObj.value( QStringLiteral( "checksum" ) ).toString().toStdString();
  const QJsonValue artifactSize = artifactObj.value( QStringLiteral( "size_bytes" ) );
  if ( artifactSize.isDouble() && artifactSize.toDouble() >= 0.0 )
    info.artifact.sizeBytes = static_cast<unsigned long long>( artifactSize.toDouble() );

  // --- Manifest v2/v3: input contract(s) --------------------------------------
  // v3: `inputs` is an array of input objects, parsed in declaration order.
  // v1/v2: the `input` object (or legacy string) fills inputs[0]. When BOTH
  // keys exist, `inputs` wins; the legacy single-input mirror always reflects
  // inputs[0] so v2 consumers (engine, tests) see no change.
  bool detConfDeclared = false; // output.detection.conf_threshold explicitly present
  const QJsonValue inputVal = obj.value( QStringLiteral( "input" ) );
  const QJsonValue inputsVal = obj.value( QStringLiteral( "inputs" ) );
  // Manifest-shape version (for manifest_version cross-checking): an `inputs`
  // array is the v3 shape, an `input` object the v2 shape, only legacy flat
  // fields the v1 shape.
  const bool inputsDeclaredAsArray = inputsVal.isArray();
  const bool inputDeclaredAsObject = inputVal.isObject();
  bool inputsMalformed = false; // declared but not a usable array-of-objects
  if ( inputsVal.isArray() )
  {
    for ( const auto &entry : inputsVal.toArray() )
    {
      if ( entry.isObject() )
        info.inputs.push_back( parseModelInputContract( entry.toObject() ) );
      else
        inputsMalformed = true;
    }
  }
  else if ( !inputsVal.isUndefined() && !inputsVal.isNull() )
  {
    inputsMalformed = true; // catalog-scan safety: wrong type must not crash the scan
  }
  if ( info.inputs.empty() && inputVal.isObject() )
    info.inputs.push_back( parseModelInputContract( inputVal.toObject() ) );
  if ( info.inputs.empty() && inputVal.isString() )
    info.inputs.push_back( ModelInputContract{} ); // v1 string form: default contract
  // Legacy flat band_roles next to the manifest root (v1/v2 fallback).
  if ( !info.inputs.empty() && info.inputs.front().bandRoles.empty() )
    info.inputs.front().bandRoles = parseStringArray( obj, QStringLiteral( "band_roles" ) );
  // Legacy single-input mirror: input = inputs.empty() ? default : inputs[0].
  info.input = info.inputs.empty() ? ModelInputContract{} : info.inputs.front();
  if ( info.inputType.empty() )
    info.inputType = info.input.dataType.empty() ? "raster" : info.input.dataType;
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
    info.output.uncertainty = outputObj.value( QStringLiteral( "uncertainty" ) ).toString().toStdString();
    if ( info.output.uncertainty.empty() )
      info.output.uncertainty = "none"; // documented default
    // Platform 4.0: raster-task output format + detection decode contract.
    info.output.format = outputObj.value( QStringLiteral( "format" ) ).toString().toStdString();
    const QJsonObject detectionObj = outputObj.value( QStringLiteral( "detection" ) ).toObject();
    if ( outputObj.contains( QStringLiteral( "detection" ) )
         && outputObj.value( QStringLiteral( "detection" ) ).isObject() )
    {
      info.output.detectionDeclared = true;
      auto &det = info.output.detection;
      det.layout = detectionObj.value( QStringLiteral( "layout" ) ).toString().toStdString();
      if ( det.layout.empty() )
        det.layout = "xywh_objectness";
      det.tensorLayout = detectionObj.value( QStringLiteral( "tensor_layout" ) ).toString().toStdString();
      if ( det.tensorLayout.empty() )
        det.tensorLayout = "auto";
      const double conf = detectionObj.value( QStringLiteral( "conf_threshold" ) ).toDouble( 0.25 );
      det.confThreshold = conf;
      detConfDeclared = detectionObj.contains( QStringLiteral( "conf_threshold" ) );
      const double iou = detectionObj.value( QStringLiteral( "nms_iou" ) ).toDouble( 0.45 );
      det.nmsIou = iou;
      det.maxDetections = detectionObj.value( QStringLiteral( "max_detections" ) ).toInt( 100000 );
      det.classes = parseStringArray( detectionObj, QStringLiteral( "classes" ) );
      // Detection classes default to the output-level classes list.
      if ( det.classes.empty() )
        det.classes = info.output.classes;
    }
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
  // Platform 4.0 device token; validated at acquire time (registry knows the
  // backend traits), so a bad token fails the run, not the catalog scan.
  info.runtime.device = runtimeObj.value( QStringLiteral( "device" ) ).toString().toStdString();

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

  // Platform 4.0 manifest_version: declared values must be 1..4 and must agree
  // with the manifest's actual shape — a declared version is a contract claim,
  // and a wrong claim means the author expects different parsing semantics
  // than the shape delivers.
  {
    const QJsonValue declaredVersion = obj.value( QStringLiteral( "manifest_version" ) );
    if ( declaredVersion.isDouble() )
    {
      const int v = declaredVersion.toInt();
      if ( v < 1 || v > 4 )
        markInvalid( "manifest_version " + std::to_string( v ) + " is unsupported (1..4)" );
      else
        info.manifestVersion = v;
    }
    else if ( !declaredVersion.isNull() && !declaredVersion.isUndefined() )
    {
      markInvalid( "manifest_version must be an integer (1..4)" );
    }
    int shapeVersion = 1;
    if ( inputsDeclaredAsArray )
      shapeVersion = 3;
    else if ( inputDeclaredAsObject )
      shapeVersion = 2;
    if ( info.manifestVersion > 0 && info.manifestVersion != shapeVersion )
      markInvalid( "declared manifest_version " + std::to_string( info.manifestVersion )
                   + " but the manifest shape is version " + std::to_string( shapeVersion )
                   + ( shapeVersion == 3 ? " ('inputs' array)" : shapeVersion == 2 ? " ('input' object)"
                                                                                   : " (legacy flat fields)" ) );
  }

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
  // Platform 4.0 follow-up vocabulary is NOT implemented yet; declaring it
  // must fail loudly instead of being silently ignored (#646 discipline).
  for ( const char *unimplemented : { "pad", "clamp" } )
  {
    if ( preObj.contains( QLatin1String( unimplemented ) ) )
      markInvalid( std::string( "preprocess." ) + unimplemented
                   + " is declared but not implemented by any runtime" );
  }
  if ( info.preprocess.scale != 1.0
       && info.preprocess.normalize != "linear" && info.preprocess.normalize != "mean_std" )
    markInvalid( "preprocess.scale is declared but normalize is neither linear nor mean_std - "
                 "the scale would silently not execute; set normalize or remove the scale" );
  // Platform 4.0: detection decode contract. With `output.detection`
  // declared, the NMS and threshold vocabulary EXECUTES (decode → threshold
  // → whole-raster NMS/tile dedup → georeferenced vector), so those fields
  // are no longer declared-but-unenforced. Without it, the historical
  // rejections stand (declared-but-unimplemented = loud failure, #646).
  if ( info.output.detectionDeclared )
  {
    if ( const std::string detectionError = info.output.detection.validate(); !detectionError.empty() )
      markInvalid( detectionError );
    if ( info.postprocess.nms )
    {
      // The legacy spelling folds into the detection contract (its nms_iou
      // governs suppression); nothing is left unenforced.
      info.postprocess.nms = false;
    }
    if ( info.output.threshold >= 0.0 )
    {
      // Legacy `output.threshold` acts as the confidence gate when the
      // detection contract did not declare its own.
      if ( !detConfDeclared )
        info.output.detection.confThreshold = info.output.threshold;
      info.output.threshold = -1.0;
    }
  }
  else
  {
    if ( info.postprocess.nms )
      markInvalid( "postprocess.nms is declared but not implemented by any runtime - remove it, or declare an output.detection contract" );
    if ( info.output.threshold >= 0.0 )
      markInvalid( "output.threshold is declared but not executed (use postprocess.mask_threshold, which the runtime enforces)" );
  }
  if ( info.postprocess.polygonize )
    markInvalid( "postprocess.polygonize is declared but not implemented by any runtime - remove it or implement mask->polygon chaining" );
  if ( info.postprocess.simplify > 0.0 )
    markInvalid( "postprocess.simplify is declared but not implemented by any runtime" );
  // Platform 4.0 raster-task output format vocabulary.
  if ( !info.output.format.empty() && info.output.format != "probability"
       && info.output.format != "labels" && info.output.format != "mask"
       && info.output.format != "confidence" )
    markInvalid( "unsupported output.format '" + info.output.format
                 + "' (supported: probability, labels, mask, confidence)" );

  // --- Manifest v3 contract validation (additive; v1/v2 unaffected) -----------
  if ( inputsMalformed )
    markInvalid( "'inputs' must be an array of input objects" );
  if ( info.inputs.size() > 1 )
  {
    // Every declared input needs a unique non-empty name so the engine can
    // bind blobs unambiguously.
    for ( size_t i = 0; i < info.inputs.size(); ++i )
    {
      const ModelInputContract &in = info.inputs[i];
      if ( in.name.empty() )
        markInvalid( "multi-input manifests need a unique name per input (input "
                     + std::to_string( i ) + " has no name)" );
      for ( size_t j = 0; j < i; ++j )
      {
        if ( !in.name.empty() && in.name == info.inputs[j].name )
          markInvalid( "multi-input manifests need a unique name per input ('"
                       + in.name + "' is declared twice)" );
      }
    }
  }
  for ( const ModelInputContract &in : info.inputs )
  {
    if ( in.temporalLength < 0 )
      markInvalid( "input.temporal_length must be >= 0 (got " + std::to_string( in.temporalLength ) + ")" );
    if ( in.temporalCollapse != "channels" )
      markInvalid( "unsupported input.temporal_collapse '" + in.temporalCollapse
                   + "' (only 'channels' is executed)" );
  }
  if ( info.output.uncertainty != "none" && info.output.uncertainty != "entropy"
       && info.output.uncertainty != "margin" )
    markInvalid( "unsupported output.uncertainty '" + info.output.uncertainty
                 + "' (supported: none, entropy, margin)" );

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

std::string ModelDetectionContract::validate() const
{
  if ( layout != "xywh_objectness" && layout != "xywh_class_scores" )
    return "output.detection.layout '" + layout
             + "' is unsupported (supported: xywh_objectness, xywh_class_scores)";
  if ( tensorLayout != "auto" && tensorLayout != "channels_first" && tensorLayout != "channels_last" )
    return "output.detection.tensor_layout '" + tensorLayout
             + "' is unsupported (supported: auto, channels_first, channels_last)";
  if ( confThreshold < 0.0 || confThreshold > 1.0 )
    return "output.detection.conf_threshold must be in [0, 1]";
  if ( nmsIou <= 0.0 || nmsIou > 1.0 )
    return "output.detection.nms_iou must be in (0, 1]";
  if ( maxDetections < 1 )
    return "output.detection.max_detections must be >= 1";
  if ( classes.empty() )
    return "output.detection.classes must declare at least one class name";
  return {};
}

std::string ModelInfo::identityTag() const
{
  const std::string idPart = stableId();
  const std::string &versionPart = modelVersion.empty() ? std::string( "0" ) : modelVersion;
  return idPart + "@" + versionPart;
}

Json::Value ModelInfo::toJson() const
{
  Json::Value out( Json::objectValue );
  out["name"] = name;
  // Platform 4.0 identity surface (additive).
  out["id"] = stableId();
  out["model_version"] = modelVersion.empty() ? "0" : modelVersion;
  if ( !license.empty() )
    out["license"] = license;
  if ( !source.empty() )
    out["source"] = source;
  if ( manifestVersion > 0 )
    out["manifest_version"] = manifestVersion;
  if ( !contentDigest.empty() )
    out["content_digest"] = contentDigest;
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
  if ( !runtime.device.empty() )
    runtimeJson["device"] = runtime.device;
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
  // Manifest v3 surface: every declared input, in order (the flat
  // "input_contract" above stays the inputs[0] mirror for v2 consumers).
  if ( !inputs.empty() )
  {
    Json::Value inputsJson( Json::arrayValue );
    for ( const ModelInputContract &in : inputs )
    {
      Json::Value inJson( Json::objectValue );
      if ( !in.name.empty() )
        inJson["name"] = in.name;
      if ( !in.dataType.empty() )
        inJson["data_type"] = in.dataType;
      if ( !in.dtype.empty() )
        inJson["dtype"] = in.dtype;
      if ( !in.layout.empty() )
        inJson["layout"] = in.layout;
      if ( !in.bandRoles.empty() )
        appendJsonArray( inJson, "band_roles", in.bandRoles );
      if ( in.width > 0 )
        inJson["width"] = in.width;
      if ( in.height > 0 )
        inJson["height"] = in.height;
      if ( in.temporalLength > 0 )
        inJson["temporal_length"] = in.temporalLength;
      if ( !in.temporalCollapse.empty() && in.temporalCollapse != "channels" )
        inJson["temporal_collapse"] = in.temporalCollapse;
      inputsJson.append( inJson );
    }
    out["inputs"] = inputsJson;
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
  if ( !output.tensorNames.empty() || !output.classes.empty() || output.threshold >= 0.0
       || output.uncertainty != "none" || !output.format.empty() || output.detectionDeclared )
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
    if ( !output.uncertainty.empty() && output.uncertainty != "none" )
      o["uncertainty"] = output.uncertainty;
    if ( !output.format.empty() )
      o["format"] = output.format;
    if ( output.detectionDeclared )
    {
      Json::Value d( Json::objectValue );
      d["layout"] = output.detection.layout;
      d["tensor_layout"] = output.detection.tensorLayout;
      d["conf_threshold"] = output.detection.confThreshold;
      d["nms_iou"] = output.detection.nmsIou;
      d["max_detections"] = output.detection.maxDetections;
      appendJsonArray( d, "classes", output.detection.classes );
      o["detection"] = d;
    }
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
  std::string checksumHex;
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

#ifdef SICNU_SOURCE_DIR
  {
      const QDir sourceModels(
          QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath( QStringLiteral( "models" ) ) );
      if ( sourceModels.exists() )
        return sourceModels.absolutePath().toStdString();
  }
#endif

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

  // Platform 4.0 identity: ALWAYS hash the artifact bytes (declared checksum
  // or not). The digest is the session-identity anchor — same path with
  // different bytes must never share a runtime session. Bounded
  // (path, size, mtime) memo so unchanged weights are hashed once.
  const qint64 mtimeMs = artifactInfo.lastModified().toMSecsSinceEpoch();
  const unsigned long long sizeBytes = static_cast<unsigned long long>( artifactInfo.size() );
  std::string actual;
  for ( const auto &verified : mVerified )
  {
    if ( verified.path == resolved && verified.sizeBytes == sizeBytes && verified.mtimeMs == mtimeMs )
    {
      actual = verified.checksumHex;
      break;
    }
  }
  if ( actual.empty() )
  {
    actual = artifactSha256Hex( resolved.toStdString() );
    if ( actual.empty() )
      return fail( ModelReadiness::ChecksumMismatch,
                   "artifact unreadable while computing content digest: " + info.resolvedArtifactPath );
    if ( mVerified.size() > 64 )
      mVerified.clear(); // bounded cache; re-hashing is only a cost, never a correctness issue
    mVerified.push_back( VerifiedArtifact{ resolved, sizeBytes, mtimeMs, actual } );
  }
  info.contentDigest = actual;

  if ( info.artifact.checksum.empty() )
    return true; // digest recorded for identity; no declared digest to enforce

  const QString expected = normalizedChecksum( info.artifact.checksum );
  if ( expected.size() != 64 )
    return fail( ModelReadiness::InvalidManifest,
                 "artifact checksum is not a valid SHA-256 hex digest" );
  if ( QString::fromStdString( actual ) != expected )
    return fail( ModelReadiness::ChecksumMismatch,
                 "artifact checksum mismatch: expected " + expected.toStdString()
                   + ", computed " + actual );
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
    std::vector<std::string> seenIds;
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
      // Platform 4.0 identity: an explicit `id` is also a uniqueness contract.
      const std::string stable = info.stableId();
      if ( std::find( seenIds.begin(), seenIds.end(), stable ) != seenIds.end() )
      {
        mIssues.push_back( { manifestPath.toStdString(),
                             "duplicate model id '" + stable + "' (first manifest wins)" } );
        continue;
      }
      seenIds.push_back( stable );

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
  std::vector<ModelInfo> combined = mModels;
  combined.insert( combined.end(), mRegistered.begin(), mRegistered.end() );
  // unregister() hides entries (scanned ones reappear on reload).
  combined.erase( std::remove_if( combined.begin(), combined.end(),
                                  [ this ]( const ModelInfo &model ) {
                                    return std::find( mUnregistered.begin(), mUnregistered.end(),
                                                      model.stableId() ) != mUnregistered.end()
                                           || std::find( mUnregistered.begin(), mUnregistered.end(),
                                                         model.name ) != mUnregistered.end();
                                  } ),
                  combined.end() );
  return combined;
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
  for ( const auto &model : mRegistered )
  {
    if ( model.task == task )
      result.push_back( model );
  }
  // unregister() hides entries (scanned ones reappear on reload).
  result.erase( std::remove_if( result.begin(), result.end(),
                                [ this ]( const ModelInfo &model ) {
                                  return std::find( mUnregistered.begin(), mUnregistered.end(),
                                                    model.stableId() ) != mUnregistered.end()
                                         || std::find( mUnregistered.begin(), mUnregistered.end(),
                                                       model.name ) != mUnregistered.end();
                                } ),
                result.end() );
  return result;
}

std::optional<ModelInfo> ModelCatalog::find( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  return findLocked( name );
}

std::optional<ModelInfo> ModelCatalog::findLocked( const std::string &idOrName ) const
{
  ensureLoadedLocked();
  auto isUnregistered = [ & ]( const ModelInfo &model ) {
    const std::string &stable = model.stableId();
    return std::find( mUnregistered.begin(), mUnregistered.end(), model.name ) != mUnregistered.end()
           || std::find( mUnregistered.begin(), mUnregistered.end(), stable ) != mUnregistered.end();
  };
  // 1) programmatic entries shadow scanned manifests;
  // 2) stable id beats name;
  // 3) name is the historical fallback.
  for ( const auto &model : mRegistered )
  {
    if ( !isUnregistered( model ) && !model.id.empty() && model.id == idOrName )
      return model;
  }
  for ( const auto &model : mModels )
  {
    if ( !isUnregistered( model ) && !model.id.empty() && model.id == idOrName )
      return model;
  }
  for ( const auto &model : mRegistered )
  {
    if ( !isUnregistered( model ) && model.name == idOrName )
      return model;
  }
  for ( const auto &model : mModels )
  {
    if ( !isUnregistered( model ) && model.name == idOrName )
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
  std::vector<ModelInfo> combined = mModels;
  combined.insert( combined.end(), mRegistered.begin(), mRegistered.end() );
  combined.erase( std::remove_if( combined.begin(), combined.end(),
                                  [ this ]( const ModelInfo &model ) {
                                    return std::find( mUnregistered.begin(), mUnregistered.end(),
                                                      model.stableId() ) != mUnregistered.end()
                                           || std::find( mUnregistered.begin(), mUnregistered.end(),
                                                         model.name ) != mUnregistered.end();
                                  } ),
                  combined.end() );

  std::vector<ModelCandidate> candidates;
  candidates.reserve( combined.size() );

  for ( const auto &model : combined )
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


// --- Platform 4.0: authoritative registry surface ----------------------------

bool ModelCatalog::registerManifestJson( const std::string &json, const std::string &source,
                                         std::string *error )
{
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson( QByteArray::fromStdString( json ), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
  {
    if ( error )
      *error = "manifest is not valid JSON: " + parseError.errorString().toStdString();
    return false;
  }
  ModelInfo info = parseManifest( doc.object(), source );
  if ( info.name.empty() )
  {
    if ( error )
      *error = "manifest has no 'name'";
    return false;
  }
  if ( info.readiness == ModelReadiness::InvalidManifest )
  {
    if ( error )
      *error = "manifest contract invalid: " + info.readinessReason;
    return false;
  }
  std::lock_guard<std::mutex> lock( catalogMutex() );
  // Artifact verification (sets Ready / MissingArtifact / ChecksumMismatch).
  // A not-ready entry still registers — the registry mirrors reality; runs
  // refuse non-ready models at execution time.
  verifyArtifactLocked( info );
  if ( info.readiness == ModelReadiness::Ready )
    info.readinessReason.clear();
  const std::string stable = info.stableId();
  mUnregistered.erase( std::remove_if( mUnregistered.begin(), mUnregistered.end(),
                                       [ & ]( const std::string &gone ) {
                                         return gone == stable || gone == info.name;
                                       } ),
                       mUnregistered.end() );
  mRegistered.erase( std::remove_if( mRegistered.begin(), mRegistered.end(),
                                     [ & ]( const ModelInfo &existing ) {
                                       return existing.stableId() == stable
                                              || existing.name == info.name;
                                     } ),
                     mRegistered.end() );
  mRegistered.push_back( std::move( info ) );
  return true;
}

bool ModelCatalog::unregister( const std::string &idOrName )
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  // Registered overlay entries are removed outright; scanned entries are
  // shadowed until the next reload().
  const std::size_t before = mRegistered.size();
  mRegistered.erase( std::remove_if( mRegistered.begin(), mRegistered.end(),
                                     [ & ]( const ModelInfo &existing ) {
                                       return existing.stableId() == idOrName
                                              || existing.name == idOrName;
                                     } ),
                     mRegistered.end() );
  if ( mRegistered.size() != before )
    return true;
  if ( findLocked( idOrName ) )
  {
    if ( std::find( mUnregistered.begin(), mUnregistered.end(), idOrName ) == mUnregistered.end() )
      mUnregistered.push_back( idOrName );
    return true;
  }
  return false;
}

namespace {

/// Catalog-level health report without re-locking (caller holds the mutex).
Json::Value healthReportLocked( const ModelInfo &model )
{
  Json::Value out( Json::objectValue );
  out["id"] = model.stableId();
  out["identity_tag"] = model.identityTag();
  out["readiness"] = modelReadinessName( model.readiness );
  out["ok"] = model.readiness == ModelReadiness::Ready;
  if ( !model.readinessReason.empty() )
    out["readiness_reason"] = model.readinessReason;
  out["artifact_present"] =
    !model.resolvedArtifactPath.empty()
    && QFileInfo( QString::fromStdString( model.resolvedArtifactPath ) ).isFile();
  out["content_digest"] = model.contentDigest;
  out["framework"] = model.framework;
  out["runtime_device"] = model.runtime.device.empty() ? "auto" : model.runtime.device;
  out["source_manifest"] = model.sourceManifest;
  return out;
}

} // namespace

Json::Value ModelCatalog::inspect( const std::string &idOrName ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  const auto model = findLocked( idOrName );
  if ( !model )
    return Json::Value( Json::nullValue );
  Json::Value out = model->toJson();
  out["identity_tag"] = model->identityTag();
  out["stable_id"] = model->stableId();
  out["health"] = healthReportLocked( *model );
  return out;
}

std::vector<std::string> ModelCatalog::validateManifestJson( const std::string &json ) const
{
  std::vector<std::string> issues;
  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson( QByteArray::fromStdString( json ), &parseError );
  if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
  {
    issues.push_back( "manifest is not valid JSON: " + parseError.errorString().toStdString() );
    return issues;
  }
  ModelInfo info = parseManifest( doc.object(), std::string() );
  if ( info.name.empty() )
    issues.push_back( "manifest has no 'name'" );
  if ( info.readiness == ModelReadiness::InvalidManifest && !info.readinessReason.empty() )
    issues.push_back( info.readinessReason );
  return issues;
}

std::optional<ModelInfo> ModelCatalog::resolve( const std::string &idVersionRef,
                                                std::string *error ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  const std::string::size_type at = idVersionRef.rfind( '@' );
  const std::string id = at == std::string::npos ? idVersionRef : idVersionRef.substr( 0, at );
  const std::string version = at == std::string::npos ? std::string() : idVersionRef.substr( at + 1 );

  std::vector<ModelInfo> matches;
  for ( const auto &model : mRegistered )
  {
    if ( model.stableId() == id )
      matches.push_back( model );
  }
  for ( const auto &model : mModels )
  {
    if ( model.stableId() == id )
      matches.push_back( model );
  }
  if ( matches.empty() )
  {
    if ( error )
      *error = "no model with id '" + id + "' in the registry";
    return std::nullopt;
  }

  auto effectiveVersion = []( const ModelInfo &model ) {
    return model.modelVersion.empty() ? std::string( "0" ) : model.modelVersion;
  };

  if ( version.empty() )
  {
    if ( matches.size() == 1 )
      return matches.front();
    // Bare-id ambiguity: resolve to the lexicographically latest version
    // (deterministic, documented) instead of failing a valid reference.
    const ModelInfo *latest = &matches.front();
    for ( const auto &candidate : matches )
    {
      if ( effectiveVersion( candidate ) > effectiveVersion( *latest ) )
        latest = &candidate;
    }
    return *latest;
  }

  for ( const auto &candidate : matches )
  {
    if ( effectiveVersion( candidate ) == version )
      return candidate;
  }
  if ( error )
  {
    std::string available;
    for ( const auto &candidate : matches )
      available += ( available.empty() ? "" : ", " ) + effectiveVersion( candidate );
    *error = "model '" + id + "' has no version '" + version + "' (available: " + available + ")";
  }
  return std::nullopt;
}

Json::Value ModelCatalog::health( const std::string &idOrName ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  const auto model = findLocked( idOrName );
  if ( !model )
    return Json::Value( Json::nullValue );
  return healthReportLocked( *model );
}

} // namespace sicnu::operators
