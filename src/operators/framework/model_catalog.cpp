// src/operators/framework/model_catalog.cpp
#include "model_catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
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

ModelInfo parseManifest( const QJsonObject &obj, const std::string &source )
{
  ModelInfo info;
  info.name = obj.value( QStringLiteral( "name" ) ).toString().toStdString();
  info.task = obj.value( QStringLiteral( "task" ) ).toString().toStdString();
  info.inputType = obj.value( QStringLiteral( "input" ) ).toString().toStdString();
  if ( info.inputType.empty() && obj.value( QStringLiteral( "input" ) ).isObject() )
  {
    info.inputType = obj.value( QStringLiteral( "input" ) ).toObject().value( QStringLiteral( "data_type" ) ).toString().toStdString();
  }
  info.outputType = obj.value( QStringLiteral( "output" ) ).toString().toStdString();
  if ( info.outputType.empty() && obj.value( QStringLiteral( "output" ) ).isObject() )
  {
    info.outputType = obj.value( QStringLiteral( "output" ) ).toObject().value( QStringLiteral( "type" ) ).toString().toStdString();
  }
  info.framework = obj.value( QStringLiteral( "framework" ) ).toString().toStdString();
  if ( info.framework.empty() )
    info.framework = "onnx";
  info.path = obj.value( QStringLiteral( "path" ) ).toString().toStdString();
  info.gpu = obj.value( QStringLiteral( "gpu" ) ).toBool( false );
  const QJsonValue accuracy = obj.value( QStringLiteral( "accuracy" ) );
  info.accuracy = accuracy.isDouble() ? accuracy.toDouble() : -1.0;
  info.description = obj.value( QStringLiteral( "description" ) ).toString().toStdString();
  for ( const auto &tag : obj.value( QStringLiteral( "tags" ) ).toArray() )
    info.tags.push_back( tag.toString().toStdString() );

  // Domain parsing
  QJsonObject domainObj = obj.value( QStringLiteral( "domain" ) ).toObject();
  QJsonArray sensorArr = domainObj.contains( QStringLiteral( "sensors" ) )
                           ? domainObj.value( QStringLiteral( "sensors" ) ).toArray()
                           : obj.value( QStringLiteral( "sensors" ) ).toArray();
  for ( const auto &s : sensorArr )
    info.sensors.push_back( s.toString().toStdString() );

  // Band roles
  QJsonObject inputObj = obj.value( QStringLiteral( "input" ) ).toObject();
  QJsonArray bandRoleArr = inputObj.contains( QStringLiteral( "band_roles" ) )
                             ? inputObj.value( QStringLiteral( "band_roles" ) ).toArray()
                             : obj.value( QStringLiteral( "band_roles" ) ).toArray();
  for ( const auto &br : bandRoleArr )
    info.supportedBandRoles.push_back( br.toString().toStdString() );

  // Resolution range
  QJsonArray resArr = domainObj.contains( QStringLiteral( "resolution_range" ) )
                        ? domainObj.value( QStringLiteral( "resolution_range" ) ).toArray()
                        : obj.value( QStringLiteral( "resolution_range" ) ).toArray();
  if ( resArr.size() >= 2 )
  {
    info.minResolutionMeters = resArr.at( 0 ).toDouble( -1.0 );
    info.maxResolutionMeters = resArr.at( 1 ).toDouble( -1.0 );
  }

  // Runtime attributes
  QJsonObject runtimeObj = obj.value( QStringLiteral( "runtime" ) ).toObject();
  if ( runtimeObj.contains( QStringLiteral( "gpu" ) ) )
    info.gpu = runtimeObj.value( QStringLiteral( "gpu" ) ).toBool( info.gpu );
  info.estimatedVramMb = runtimeObj.value( QStringLiteral( "estimated_vram_mb" ) ).toInt(
    obj.value( QStringLiteral( "estimated_vram_mb" ) ).toInt( 0 ) );
  info.supportsTiling = runtimeObj.value( QStringLiteral( "supports_tiling" ) ).toBool(
    obj.value( QStringLiteral( "supports_tiling" ) ).toBool( true ) );
  info.cpuFallback = runtimeObj.value( QStringLiteral( "cpu_fallback" ) ).toBool(
    obj.value( QStringLiteral( "cpu_fallback" ) ).toBool( true ) );

  info.sourceManifest = source;
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
  {
    Json::Value tagArray( Json::arrayValue );
    for ( const auto &tag : tags )
      tagArray.append( tag );
    out["tags"] = tagArray;
  }
  if ( !sensors.empty() )
  {
    Json::Value sensorArray( Json::arrayValue );
    for ( const auto &s : sensors )
      sensorArray.append( s );
    out["sensors"] = sensorArray;
  }
  if ( !supportedBandRoles.empty() )
  {
    Json::Value bandArray( Json::arrayValue );
    for ( const auto &br : supportedBandRoles )
      bandArray.append( br );
    out["band_roles"] = bandArray;
  }
  if ( minResolutionMeters >= 0.0 || maxResolutionMeters >= 0.0 )
  {
    Json::Value resRange( Json::arrayValue );
    resRange.append( minResolutionMeters );
    resRange.append( maxResolutionMeters );
    out["resolution_range"] = resRange;
  }
  Json::Value runtime( Json::objectValue );
  runtime["gpu"] = gpu;
  runtime["estimated_vram_mb"] = estimatedVramMb;
  runtime["supports_tiling"] = supportsTiling;
  runtime["cpu_fallback"] = cpuFallback;
  out["runtime"] = runtime;
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

void ModelCatalog::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;

  mModels.clear();
  const QDir dir( QString::fromStdString( mDirectory.empty() ? defaultModelsDirectory() : mDirectory ) );
  if ( dir.exists() )
  {
    const auto entries = dir.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );
    for ( const auto &entry : entries )
    {
      const QString manifestPath = QDir( entry.absoluteFilePath() ).filePath( QStringLiteral( "model.json" ) );
      QFile file( manifestPath );
      if ( !file.open( QIODevice::ReadOnly ) )
        continue;

      const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
      if ( !doc.isObject() )
        continue;

      ModelInfo info = parseManifest( doc.object(), manifestPath.toStdString() );
      if ( info.name.empty() )
        continue;
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

    // 3. Resolution range check
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

    // 4. Hardware and GPU availability
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

    // 5. Benchmark accuracy factor
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
    return a.score > b.score;
  } );

  return candidates;
}

} // namespace sicnu::operators
