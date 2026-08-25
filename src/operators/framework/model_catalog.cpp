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
  info.outputType = obj.value( QStringLiteral( "output" ) ).toString().toStdString();
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
  out["sourceManifest"] = sourceManifest;
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
  }
  reload();
}

std::string ModelCatalog::directory() const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  return mDirectory.empty() ? defaultModelsDirectory() : mDirectory;
}

void ModelCatalog::reload()
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  mModels.clear();

  const QDir dir( QString::fromStdString( mDirectory.empty() ? defaultModelsDirectory() : mDirectory ) );
  if ( !dir.exists() )
    return;

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

std::vector<ModelInfo> ModelCatalog::models() const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
  return mModels;
}

std::vector<ModelInfo> ModelCatalog::modelsByTask( const std::string &task ) const
{
  std::lock_guard<std::mutex> lock( catalogMutex() );
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
  for ( const auto &model : mModels )
  {
    if ( model.name == name )
      return model;
  }
  return std::nullopt;
}

} // namespace sicnu::operators
