// src/processing/algorithms/temporal/temporal_workspace.cpp
#include "temporal_workspace.h"

#include "data/data_manager.h"

#include "data/derivation_record.h"

#include <QUuid>

#include <sstream>

namespace sicnu::temporal
{

namespace
{

QString descriptorToJsonText( const Json::Value &v )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = ""; // canonical, compact — the document is data, not UI
  std::ostringstream oss;
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( v, &oss );
  return QString::fromStdString( oss.str() );
}

bool descriptorFromJsonText( const QString &text, Json::Value *out, QString *error )
{
  const QByteArray raw = text.toUtf8();
  Json::CharReaderBuilder readerBuilder;
  std::unique_ptr<Json::CharReader> reader( readerBuilder.newCharReader() );
  Json::Value v;
  std::string parseErr;
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &v, &parseErr ) )
  {
    if ( error )
      *error = QStringLiteral( "invalid collection descriptor JSON: %1" )
                 .arg( QString::fromStdString( parseErr ) );
    return false;
  }
  if ( out )
    *out = std::move( v );
  return true;
}

} // namespace

namespace
{
// Set once by the host at startup; read-only afterwards, so no locking
// beyond the startup race (which the startup sequence excludes).
sicnu::data::DataManager *s_workspaceCatalog = nullptr;
} // namespace

void setWorkspaceCatalog( sicnu::data::DataManager *catalog )
{
  s_workspaceCatalog = catalog;
}

sicnu::data::DataManager *workspaceCatalog()
{
  return s_workspaceCatalog;
}

QString collectionDescriptorText( const TemporalCollection &collection )
{
  return descriptorToJsonText( collection.toJson() );
}

bool collectionFromDescriptorText( const QString &descriptor, TemporalCollection *out,
                                   QString *error )
{
  Json::Value v;
  if ( !descriptorFromJsonText( descriptor, &v, error ) )
    return false;
  return TemporalCollection::fromJson( v, out, error );
}

int bindCollectionAssets( TemporalCollection &collection, sicnu::data::DataManager *dataManager )
{
  if ( !dataManager )
    return 0;
  int bound = 0;
  for ( TemporalSceneRef &scene : collection.scenes() )
  {
    const auto snapshot = dataManager->findByPath( scene.path );
    if ( !snapshot )
      continue;
    scene.assetId = snapshot->id().toString();
    scene.assetRevision = QString::number( snapshot->revision().value() );
    ++bound;
  }
  return bound;
}

sicnu::data::CollectionId saveCollectionToWorkspace( sicnu::data::DataManager &dataManager,
                                                     const QString &displayName,
                                                     const TemporalCollection &collection,
                                                     const sicnu::data::CollectionId &existingId,
                                                     QString *error,
                                                     bool *reusedExisting )
{
  TemporalCollection bound = collection;
  if ( !displayName.isEmpty() )
    bound.setName( displayName );
  bindCollectionAssets( bound, &dataManager );

  const sicnu::data::TemporalCollectionCreateRequest request{
    displayName, collectionDescriptorText( bound ) };

  if ( !existingId.isNull() )
  {
    const auto updated = dataManager.updateTemporalCollection( existingId, request );
    if ( !updated )
    {
      if ( error )
        *error = updated.diagnostics().isEmpty()
                   ? QStringLiteral( "cannot update temporal collection record" )
                   : updated.diagnostics().front().message;
      return sicnu::data::CollectionId();
    }
    return existingId;
  }

  const auto created = dataManager.createTemporalCollection( request );
  if ( created.collectionId.isNull() )
  {
    if ( error )
      *error = created.diagnostics.isEmpty()
                 ? QStringLiteral( "cannot create temporal collection record" )
                 : created.diagnostics.front().message;
    return sicnu::data::CollectionId();
  }
  if ( reusedExisting )
    *reusedExisting = created.reusedExisting;
  return created.collectionId;
}

bool loadCollectionFromWorkspace( sicnu::data::DataManager &dataManager, const sicnu::data::CollectionId &id,
                                  TemporalCollection *out, QString *error )
{
  const auto record = dataManager.temporalCollection( id );
  if ( !record )
  {
    if ( error )
      *error = QStringLiteral( "no temporal collection record with this id" );
    return false;
  }
  TemporalCollection collection;
  if ( !collectionFromDescriptorText( record->descriptor, &collection, error ) )
    return false;
  if ( out )
    *out = std::move( collection );
  return true;
}

bool collectionFingerprintInputs( sicnu::data::DataManager &dataManager, const sicnu::data::CollectionId &id,
                                  const TemporalCollection &collection,
                                  QVector<sicnu::data::TaggedDerivationInput> *out )
{
  if ( !out || id.isNull() )
    return false;
  const auto record = dataManager.temporalCollection( id );
  if ( !record )
    return false;

  // Collection-level identity: record id @ record revision. Bumping the
  // record (a scene re-bind, a rename that changes the document, an explicit
  // update) invalidates every cached step that consumed the collection.
  const auto collectionAssetId = sicnu::data::AssetId::fromString( id.toString() );
  if ( !collectionAssetId )
    return false;

  QVector<sicnu::data::TaggedDerivationInput> inputs;
  sicnu::data::TaggedDerivationInput collectionInput;
  collectionInput.assetId = *collectionAssetId;
  collectionInput.revision = sicnu::data::AssetRevision::fromValue( record->revision );
  collectionInput.toPort = QStringLiteral( "collection" );
  collectionInput.valueDomain = QStringLiteral( "temporal_collection" );
  inputs.append( collectionInput );

  // Scene identity: the scene's CURRENT asset revision, resolved live — a
  // scene re-commit (revision bump) must invalidate the cached step even
  // when the stored descriptor still carries the old revision.
  for ( const TemporalSceneRef &scene : collection.scenes() )
  {
    const auto snapshot = dataManager.findByPath( scene.path );
    if ( !snapshot )
      return false; // unbound scene: no provable data identity → uncacheable
    sicnu::data::TaggedDerivationInput sceneInput;
    sceneInput.assetId = snapshot->id();
    sceneInput.revision = snapshot->revision();
    sceneInput.toPort = QStringLiteral( "scene" );
    sceneInput.valueDomain = QStringLiteral( "raster" );
    inputs.append( sceneInput );
  }

  *out = std::move( inputs );
  return true;
}

bool fingerprintInputsForCollectionParam( sicnu::data::DataManager *dataManager, const QString &collectionParam,
                                          QVector<sicnu::data::TaggedDerivationInput> *out,
                                          QString *reason )
{
  auto fail = [reason]( const QString &message ) {
    if ( reason )
      *reason = message;
    return false;
  };
  if ( !dataManager )
    return fail( QStringLiteral( "no DataManager wired" ) );
  if ( collectionParam.trimmed().isEmpty() )
    return fail( QStringLiteral( "empty collection parameter" ) );

  TemporalCollection collection;
  const QUuid maybeId( collectionParam.trimmed() );
  if ( !maybeId.isNull() )
  {
    const auto id = sicnu::data::CollectionId::fromString( collectionParam.trimmed() );
    if ( !id )
      return fail( QStringLiteral( "malformed collection id" ) );
    if ( !loadCollectionFromWorkspace( *dataManager, *id, &collection, nullptr ) )
      return fail( QStringLiteral( "collection id does not address a workspace record" ) );
    if ( !collectionFingerprintInputs( *dataManager, *id, collection, out ) )
      return fail( QStringLiteral( "collection has unbound (path-only) scenes" ) );
    return true;
  }

  // Descriptor file path: only cacheable when the descriptor is identical to
  // a registered workspace record (so a revision-aware identity exists).
  const auto records = dataManager->temporalCollections();
  if ( records.isEmpty() )
    return fail( QStringLiteral( "no workspace records registered" ) );
  TemporalCollection fromFile;
  if ( !TemporalCollection::load( collectionParam, &fromFile, nullptr ) )
    return fail( QStringLiteral( "collection descriptor file cannot be loaded" ) );
  const QString fileDescriptor = collectionDescriptorText( fromFile );
  for ( const auto &record : records )
  {
    if ( record.descriptor != fileDescriptor )
      continue;
    if ( !collectionFingerprintInputs( *dataManager, record.id, fromFile, out ) )
      return fail( QStringLiteral( "collection has unbound (path-only) scenes" ) );
    return true;
  }
  return fail( QStringLiteral( "descriptor does not match any workspace record" ) );
}

bool fingerprintInputsForOperatorParams( sicnu::data::DataManager *dataManager,
                                         const QVariantMap &params, const QString &outputPath,
                                         QVector<sicnu::data::TaggedDerivationInput> *out,
                                         QString *reason )
{
  auto fail = [reason]( const QString &message ) {
    if ( reason )
      *reason = message;
    return false;
  };
  if ( !dataManager )
    return fail( QStringLiteral( "no catalog wired" ) );

  const QString collectionParam = params.value( QStringLiteral( "collection" ) ).toString();

  // Inline scene lists ("scenes": [path | {path, ...}, ...]) are explicit
  // inputs in both shapes; the generic string-path scan below cannot see
  // paths nested in objects.
  QStringList scenePaths;
  const QVariant scenesValue = params.value( QStringLiteral( "scenes" ) );
  const std::function<void( const QVariant & )> collectScene =
    [&]( const QVariant &entry ) {
      if ( entry.userType() == QMetaType::QString )
      {
        scenePaths.append( entry.toString() );
      }
      else if ( entry.userType() == QMetaType::QVariantMap )
      {
        scenePaths.append( entry.toMap().value( QStringLiteral( "path" ) ).toString() );
      }
      else if ( entry.userType() == QMetaType::QVariantList )
      {
        for ( const QVariant &item : entry.toList() )
          collectScene( item );
      }
    };
  if ( scenesValue.userType() == QMetaType::QVariantList )
  {
    for ( const QVariant &item : scenesValue.toList() )
      collectScene( item );
  }
  scenePaths.removeAll( QString() );

  // Exclusions: the destination (not identity), the collection parameter
  // (resolved through the workspace below) and the inline scene paths
  // (resolved explicitly below).
  QStringList exclude;
  if ( !outputPath.isEmpty() )
    exclude.append( outputPath );
  if ( !collectionParam.isEmpty() )
    exclude.append( collectionParam );
  exclude.append( scenePaths );

  // 1) Generic string/list path parameters — every one must resolve to a
  //    registered asset, or the step is not cacheable.
  const QStringList paths = sicnu::data::findInputPathsInParams( params, exclude );
  for ( const QString &path : paths )
  {
    const auto snapshot = dataManager->findByPath( path );
    if ( !snapshot )
      return fail( QStringLiteral( "unresolved input path: %1" ).arg( path ) );
    sicnu::data::TaggedDerivationInput input;
    input.assetId = snapshot->id();
    input.revision = snapshot->revision();
    input.toPort = QStringLiteral( "input" );
    input.valueDomain = QStringLiteral( "raster" );
    out->append( input );
  }

  // 2) Inline scene entries — same rule.
  for ( const QString &path : scenePaths )
  {
    const auto snapshot = dataManager->findByPath( path );
    if ( !snapshot )
      return fail( QStringLiteral( "unresolved scene path: %1" ).arg( path ) );
    sicnu::data::TaggedDerivationInput input;
    input.assetId = snapshot->id();
    input.revision = snapshot->revision();
    input.toPort = QStringLiteral( "scene" );
    input.valueDomain = QStringLiteral( "raster" );
    out->append( input );
  }

  // 3) Workspace-bound temporal collection.
  if ( !collectionParam.isEmpty() &&
       !fingerprintInputsForCollectionParam( dataManager, collectionParam, out, reason ) )
  {
    if ( reason && reason->isEmpty() )
      *reason = QStringLiteral( "collection is not revision-identifiable" );
    return false;
  }
  return true;
}

} // namespace sicnu::temporal
