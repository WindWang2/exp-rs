// src/processing/framework/algorithm_meta_store.cpp
#include "algorithm_meta_store.h"

#include "runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <mutex>

namespace sicnu::processing {

namespace {

std::mutex &storeMutex()
{
  static std::mutex mutex;
  return mutex;
}

} // namespace

Json::Value AlgorithmMetaEntry::toJson() const
{
  Json::Value out( Json::objectValue );
  out["id"] = id;
  out["task"] = task;
  out["input"] = input;
  out["output"] = output;
  out["gpu"] = gpu;
  if ( accuracy >= 0.0 )
    out["accuracy"] = accuracy;
  if ( !notes.empty() )
    out["notes"] = notes;
  if ( !tags.empty() )
  {
    Json::Value tagArray( Json::arrayValue );
    for ( const auto &tag : tags )
      tagArray.append( tag );
    out["tags"] = tagArray;
  }
  return out;
}

AlgorithmMetaStore &AlgorithmMetaStore::instance()
{
  static AlgorithmMetaStore store;
  return store;
}

size_t AlgorithmMetaStore::loadFromDirectory( const std::string &dir )
{
  std::lock_guard<std::mutex> lock( storeMutex() );
  mEntries.clear();

  const QDir sidecarDir( QString::fromStdString( dir ) );
  if ( !sidecarDir.exists() )
    return 0;

  const auto files = sidecarDir.entryInfoList( { QStringLiteral( "*.json" ) }, QDir::Files, QDir::Name );
  for ( const auto &file : files )
  {
    QFile handle( file.absoluteFilePath() );
    if ( !handle.open( QIODevice::ReadOnly ) )
      continue;

    const QJsonDocument doc = QJsonDocument::fromJson( handle.readAll() );
    if ( !doc.isObject() )
      continue;

    const QJsonObject obj = doc.object();
    AlgorithmMetaEntry entry;
    entry.id = obj.value( QStringLiteral( "id" ) ).toString().toStdString();
    if ( entry.id.empty() )
      continue;
    entry.task = obj.value( QStringLiteral( "task" ) ).toString().toStdString();
    entry.input = obj.value( QStringLiteral( "input" ) ).toString().toStdString();
    entry.output = obj.value( QStringLiteral( "output" ) ).toString().toStdString();
    entry.gpu = obj.value( QStringLiteral( "gpu" ) ).toBool( false );
    const QJsonValue accuracy = obj.value( QStringLiteral( "accuracy" ) );
    entry.accuracy = accuracy.isDouble() ? accuracy.toDouble() : -1.0;
    entry.notes = obj.value( QStringLiteral( "notes" ) ).toString().toStdString();
    for ( const auto &tag : obj.value( QStringLiteral( "tags" ) ).toArray() )
      entry.tags.push_back( tag.toString().toStdString() );

    mEntries.emplace( entry.id, std::move( entry ) );
  }
  return mEntries.size();
}

size_t AlgorithmMetaStore::loadDefaults()
{
  return loadFromDirectory(
    resolveRuntimeDataPath( QStringLiteral( "data/processing/algorithm_meta" ) ).toStdString() );
}

std::optional<AlgorithmMetaEntry> AlgorithmMetaStore::find( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( storeMutex() );
  const auto it = mEntries.find( id );
  if ( it == mEntries.end() )
    return std::nullopt;
  return it->second;
}

std::vector<AlgorithmMetaEntry> AlgorithmMetaStore::entries() const
{
  std::lock_guard<std::mutex> lock( storeMutex() );
  std::vector<AlgorithmMetaEntry> result;
  result.reserve( mEntries.size() );
  for ( const auto &[id, entry] : mEntries )
    result.push_back( entry );
  return result;
}

size_t AlgorithmMetaStore::size() const
{
  std::lock_guard<std::mutex> lock( storeMutex() );
  return mEntries.size();
}

} // namespace sicnu::processing
