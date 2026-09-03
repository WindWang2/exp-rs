#include <QFileInfo>
#include <algorithm>
#include <functional>
#include "derivation_record.h"

#include "data_manager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::data
{

namespace
{

const QString kAlgorithmId = QStringLiteral( "algorithmId" );
const QString kAlgorithmVersion = QStringLiteral( "algorithmVersion" );
const QString kParameters = QStringLiteral( "parameters" );
const QString kInputs = QStringLiteral( "inputs" );
const QString kAssetId = QStringLiteral( "assetId" );
const QString kRevision = QStringLiteral( "revision" );
const QString kBandReferences = QStringLiteral( "bandReferences" );
const QString kValueDomain = QStringLiteral( "valueDomain" );
const QString kOutputAssetId = QStringLiteral( "outputAssetId" );
const QString kTaskReference = QStringLiteral( "taskReference" );
const QString kSoftwareVersion = QStringLiteral( "softwareVersion" );
const QString kCompletedAt = QStringLiteral( "completedAt" );
const QString kAuthConfigId = QStringLiteral( "authConfigId" );

QJsonObject inputToJson( const DerivationInput &input )
{
  QJsonObject json;
  json.insert( kAssetId, input.assetId.isNull() ? QString() : input.assetId.toString() );
  json.insert( kRevision,
               QJsonValue::fromVariant( QVariant::fromValue( input.revision.value() ) ) );
  json.insert( kBandReferences, QJsonArray::fromStringList( input.bandReferences ) );
  json.insert( kValueDomain, input.valueDomain );
  return json;
}

Result<AssetId> assetIdFromJson( const QJsonObject &json, const QString &key )
{
  const QString text = json.value( key ).toString();
  if ( text.isEmpty() )
    return Result<AssetId>::success( AssetId() );
  const std::optional<AssetId> assetId = AssetId::fromString( text );
  if ( !assetId )
  {
    return Result<AssetId>::failure(
      { QStringLiteral( "derivation.invalid" ),
        QStringLiteral( "%1 is not a valid Asset ID: %2" ).arg( key ).arg( text ) } );
  }
  return Result<AssetId>::success( *assetId );
}

Result<DerivationInput> inputFromJson( const QJsonObject &json )
{
  DerivationInput input;
  Result<AssetId> assetId = assetIdFromJson( json, kAssetId );
  if ( !assetId )
    return Result<DerivationInput>::failure( assetId.diagnostics() );
  input.assetId = assetId.value();
  input.revision = AssetRevision::fromValue(
    static_cast<quint64>( json.value( kRevision ).toInteger() ) );
  const QJsonArray bandReferences = json.value( kBandReferences ).toArray();
  for ( const QJsonValue &bandReference : bandReferences )
    input.bandReferences.append( bandReference.toString() );
  input.valueDomain = json.value( kValueDomain ).toString();
  return Result<DerivationInput>::success( input );
}


} // namespace

QJsonObject DerivationRecord::toJson() const
{
  QJsonObject json;
  json.insert( kAlgorithmId, algorithmId );
  json.insert( kAlgorithmVersion, algorithmVersion );
  json.insert( kParameters, parameters );

  QJsonArray inputsJson;
  for ( const DerivationInput &input : inputs )
    inputsJson.append( inputToJson( input ) );
  json.insert( kInputs, inputsJson );

  if ( !unresolvedInputPaths.isEmpty() )
    json.insert( QStringLiteral( "unresolvedInputPaths" ),
                 QJsonArray::fromStringList( unresolvedInputPaths ) );

  json.insert( kOutputAssetId, outputAssetId.isNull() ? QString() : outputAssetId.toString() );
  json.insert( kTaskReference, taskReference );
  json.insert( kSoftwareVersion, softwareVersion );
  json.insert( kCompletedAt, completedAtUtc.toString( Qt::ISODateWithMs ) );
  json.insert( kAuthConfigId, authConfigId );
  if ( !executionFingerprint.isEmpty() )
    json.insert( QStringLiteral( "executionFingerprint" ), executionFingerprint );
  json.insert( QStringLiteral( "workflowId" ), workflowId );
  json.insert( QStringLiteral( "workflowRunId" ), workflowRunId );
  json.insert( QStringLiteral( "stepId" ), stepId );
  if ( collectionId )
  {
    json.insert( QStringLiteral( "collectionId" ), collectionId->toString() );
    json.insert( QStringLiteral( "collectionRevision" ), static_cast<qint64>( collectionRevision ) );
  }
  if ( cacheHit )
    json.insert( QStringLiteral( "cacheHit" ), true );
  return json;
}

Result<DerivationRecord> DerivationRecord::fromJson( const QJsonObject &json )
{
  DerivationRecord record;
  record.algorithmId = json.value( kAlgorithmId ).toString();
  record.algorithmVersion = json.value( kAlgorithmVersion ).toString();
  record.parameters = json.value( kParameters ).toObject();

  const QJsonArray inputsJson = json.value( kInputs ).toArray();
  for ( const QJsonValue &inputValue : inputsJson )
  {
    Result<DerivationInput> input = inputFromJson( inputValue.toObject() );
    if ( !input )
      return Result<DerivationRecord>::failure( input.diagnostics() );
    record.inputs.append( input.take() );
  }

  const QJsonArray unresolvedJson = json.value( QStringLiteral( "unresolvedInputPaths" ) ).toArray();
  for ( const QJsonValue &unresolvedValue : unresolvedJson )
    record.unresolvedInputPaths.append( unresolvedValue.toString() );

  Result<AssetId> outputAssetId = assetIdFromJson( json, kOutputAssetId );
  if ( !outputAssetId )
    return Result<DerivationRecord>::failure( outputAssetId.diagnostics() );
  record.outputAssetId = outputAssetId.value();

  record.taskReference = json.value( kTaskReference ).toString();
  record.softwareVersion = json.value( kSoftwareVersion ).toString();
  const QString completedAtText = json.value( kCompletedAt ).toString();
  if ( !completedAtText.isEmpty() )
  {
    record.completedAtUtc = QDateTime::fromString( completedAtText, Qt::ISODateWithMs );
    if ( !record.completedAtUtc.isValid() )
    {
      return Result<DerivationRecord>::failure(
        { QStringLiteral( "derivation.invalid" ),
          QStringLiteral( "completedAt is not a valid ISO timestamp: %1" ).arg( completedAtText ) } );
    }
  }
  record.authConfigId = json.value( kAuthConfigId ).toString();
  record.executionFingerprint = json.value( QStringLiteral( "executionFingerprint" ) ).toString();
  record.workflowId = json.value( QStringLiteral( "workflowId" ) ).toString();
  record.workflowRunId = json.value( QStringLiteral( "workflowRunId" ) ).toString();
  record.stepId = json.value( QStringLiteral( "stepId" ) ).toString();
  if ( json.contains( QStringLiteral( "collectionId" ) ) )
  {
    const QString colIdStr = json.value( QStringLiteral( "collectionId" ) ).toString();
    if ( !colIdStr.isEmpty() )
    {
      const auto parsedId = CollectionId::fromString( colIdStr );
      if ( parsedId )
        record.collectionId = *parsedId;
    }
    if ( json.contains( QStringLiteral( "collectionRevision" ) ) )
      record.collectionRevision = json.value( QStringLiteral( "collectionRevision" ) ).toVariant().toULongLong();
  }
  if ( json.contains( QStringLiteral( "cacheHit" ) ) )
    record.cacheHit = json.value( QStringLiteral( "cacheHit" ) ).toBool();
  return Result<DerivationRecord>::success( record );
}


bool isOutputVocabularyKey( const QString &key )
{
  return key.contains( QStringLiteral( "output" ), Qt::CaseInsensitive )
         || key.contains( QStringLiteral( "result" ), Qt::CaseInsensitive );
}

QStringList findInputPathsInParams( const QVariantMap &params,
                                    const QStringList &excludePaths )
{
  QStringList paths;
  // The run's own destinations, in the forms a parameter may spell them: the
  // exact caller-supplied paths plus their canonical resolutions, so a re-run
  // over an existing output cannot record the output as its own source even
  // when the destination rode under a non-"output"-like key (review of #718:
  // "result_path"/"modelOut"-style spellings are TaskCenter output
  // vocabulary but slipped past a key-name-only guard).
  QStringList excludedForms;
  for ( const QString &excludePath : excludePaths )
  {
    if ( excludePath.isEmpty() )
      continue;
    if ( !excludedForms.contains( excludePath ) )
      excludedForms.append( excludePath );
    const QString canonical = QFileInfo( excludePath ).canonicalFilePath();
    if ( !canonical.isEmpty() && !excludedForms.contains( canonical ) )
      excludedForms.append( canonical );
  }
  std::function<void( const QVariant & )> collect = [ & ]( const QVariant &value ) {
    QStringList candidates;
    if ( value.userType() == QMetaType::QStringList )
      candidates = value.toStringList();
    else if ( value.userType() == QMetaType::QVariantList )
    {
      const QVariantList list = value.toList();
      for ( const QVariant &item : list )
        collect( item );
      return;
    }
    else
      candidates.append( value.toString() );

    for ( const QString &candidate : candidates )
    {
      const QString trimmed = candidate.trimmed();
      if ( trimmed.isEmpty() )
        continue;
      // A surviving placeholder reference IS an input reference (#727):
      // after substitution it should have become a real path; if it still
      // starts with '$' the substitution failed (e.g. the parent's port was
      // missing). Collect it — it cannot resolve to an asset, so
      // resolveInputLineage reports it in unresolvedPaths instead of the
      // reference vanishing from provenance (the silent
      // inputs=[]/unresolvedInputPaths=[] state).
      if ( trimmed.startsWith( QLatin1Char( '$' ) ) )
      {
        if ( !paths.contains( trimmed ) )
          paths.append( trimmed );
        continue;
      }
      // Only existing FILES identify an input asset; directories and
      // not-yet-written paths cannot resolve.
      if ( !QFileInfo( trimmed ).isFile() )
        continue;
      const bool isOwnDestination = std::any_of(
        excludedForms.cbegin(), excludedForms.cend(), [ &trimmed ]( const QString &form ) {
          if ( trimmed == form )
            return true;
          const QString canonical = QFileInfo( trimmed ).canonicalFilePath();
          return !canonical.isEmpty() && canonical == form;
        } );
      if ( !isOwnDestination && !paths.contains( trimmed ) )
        paths.append( trimmed );
    }
  };

  // Full scan (#718): lineage edges live on whatever key carries the path
  // ("input", but also dNBR's "postfire" or fusion's "pan"/"ms"). The run's
  // own destination must never pose as an input: "output" AND "result"
  // spellings are both skipped because that is exactly TaskCenter's
  // findOutputPathInParams vocabulary — whatever those keys carry is a
  // destination, not a source (and the value-level exclusion above covers a
  // destination that rode under a non-vocabulary key).
  for ( auto it = params.begin(); it != params.end(); ++it )
  {
    if ( isOutputVocabularyKey( it.key() ) )
      continue;
    collect( it.value() );
  }
  return paths;
}

InputLineage resolveInputLineage( DataManager *dataManager, const QStringList &paths )
{
  InputLineage lineage;
  if ( !dataManager )
  {
    // No catalog wired: nothing can resolve. Keep every path visible.
    lineage.unresolvedPaths = paths;
    return lineage;
  }
  for ( const QString &path : paths )
  {
    // Registered assets store a canonicalized source; compare the canonical
    // form so symlinked / case-variant parameter paths still resolve (a plain
    // absoluteFilePath match silently dropped those lineage edges) (#698
    // review, restored for #718).
    const QFileInfo fi( path );
    const QString canonical = fi.canonicalFilePath();
    const auto snapshot =
      dataManager->findByPath( canonical.isEmpty() ? path : canonical );
    if ( !snapshot )
    {
      lineage.unresolvedPaths.append( path );
      continue;
    }
    DerivationInput input;
    input.assetId = snapshot->id();
    input.revision = snapshot->revision();
    lineage.inputs.append( input );
  }
  return lineage;
}

InputLineage resolveInputLineageForParams( DataManager *dataManager,
                                          const QVariantMap &params,
                                          const QStringList &excludePaths )
{
  // 1. Resolve standard parameter file paths
  const QStringList genericPaths = findInputPathsInParams( params, excludePaths );
  InputLineage lineage = resolveInputLineage( dataManager, genericPaths );

  if ( !dataManager )
    return lineage;

  // 2. Resolve temporal collection parameter when present
  if ( params.contains( QStringLiteral( "collection" ) ) )
  {
    const QString colParam = params.value( QStringLiteral( "collection" ) ).toString().trimmed();
    if ( !colParam.isEmpty() )
    {
      std::optional<CollectionId> colId = CollectionId::fromString( colParam );
      std::optional<TemporalCollectionRecord> colRecord;
      if ( colId )
      {
        colRecord = dataManager->temporalCollection( *colId );
      }
      else
      {
        if ( QFile::exists( colParam ) )
        {
          QFile f( colParam );
          if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
          {
            const QString content = QString::fromUtf8( f.readAll() );
            for ( const auto &rec : dataManager->temporalCollections() )
            {
              if ( rec.descriptor == content )
              {
                colRecord = rec;
                colId = rec.id;
                break;
              }
            }
          }
        }
      }

      if ( colRecord )
      {
        lineage.collectionId = colRecord->id;
        lineage.collectionRevision = colRecord->revision;

        const auto colAssetId = AssetId::fromString( colRecord->id.toString() );
        if ( colAssetId )
        {
          DerivationInput colInput;
          colInput.assetId = *colAssetId;
          colInput.revision = AssetRevision::fromValue( colRecord->revision );
          colInput.valueDomain = QStringLiteral( "temporal_collection" );
          if ( !lineage.inputs.contains( colInput ) )
            lineage.inputs.append( colInput );
        }

        const QByteArray jsonBytes = colRecord->descriptor.toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson( jsonBytes );
        if ( doc.isObject() )
        {
          const QJsonArray scenesArr = doc.object().value( QStringLiteral( "scenes" ) ).toArray();
          for ( const QJsonValue &scVal : scenesArr )
          {
            if ( !scVal.isObject() )
              continue;
            const QJsonObject scObj = scVal.toObject();
            const QString scenePath = scObj.value( QStringLiteral( "path" ) ).toString();
            bool sceneResolved = false;

            if ( !scenePath.isEmpty() )
            {
              const QFileInfo fi( scenePath );
              const QString canonical = fi.canonicalFilePath();
              const auto snapshot = dataManager->findByPath( canonical.isEmpty() ? scenePath : canonical );
              if ( snapshot )
              {
                DerivationInput scIn;
                scIn.assetId = snapshot->id();
                scIn.revision = snapshot->revision();
                scIn.valueDomain = QStringLiteral( "raster" );
                if ( !lineage.inputs.contains( scIn ) )
                  lineage.inputs.append( scIn );
                sceneResolved = true;
              }
            }
            if ( !sceneResolved && scObj.contains( QStringLiteral( "asset_id" ) ) )
            {
              const auto aid = AssetId::fromString( scObj.value( QStringLiteral( "asset_id" ) ).toString() );
              if ( aid )
              {
                const auto snapshot = dataManager->asset( *aid );
                if ( snapshot )
                {
                  DerivationInput scIn;
                  scIn.assetId = snapshot->id();
                  scIn.revision = snapshot->revision();
                  scIn.valueDomain = QStringLiteral( "raster" );
                  if ( !lineage.inputs.contains( scIn ) )
                    lineage.inputs.append( scIn );
                  sceneResolved = true;
                }
              }
            }
            if ( !sceneResolved && !scenePath.isEmpty() )
            {
              if ( !lineage.unresolvedPaths.contains( scenePath ) )
                lineage.unresolvedPaths.append( scenePath );
            }
          }
        }
      }
      else
      {
        if ( !lineage.unresolvedPaths.contains( colParam ) )
          lineage.unresolvedPaths.append( colParam );
      }
    }
  }

  // 3. Resolve inline scenes parameter when present
  if ( params.contains( QStringLiteral( "scenes" ) ) )
  {
    const QVariant scVar = params.value( QStringLiteral( "scenes" ) );
    QVariantList scList;
    if ( scVar.userType() == QMetaType::QStringList )
    {
      for ( const QString &s : scVar.toStringList() )
        scList.append( s );
    }
    else if ( scVar.userType() == QMetaType::QVariantList )
    {
      scList = scVar.toList();
    }
    for ( const QVariant &entry : scList )
    {
      QString scPath;
      QString assetIdStr;
      if ( entry.userType() == QMetaType::QString )
      {
        scPath = entry.toString();
      }
      else if ( entry.userType() == QMetaType::QVariantMap )
      {
        const QVariantMap m = entry.toMap();
        scPath = m.value( QStringLiteral( "path" ) ).toString();
        assetIdStr = m.value( QStringLiteral( "asset_id" ) ).toString();
      }
      bool resolved = false;
      if ( !scPath.isEmpty() )
      {
        const QFileInfo fi( scPath );
        const QString canonical = fi.canonicalFilePath();
        const auto snapshot = dataManager->findByPath( canonical.isEmpty() ? scPath : canonical );
        if ( snapshot )
        {
          DerivationInput scIn;
          scIn.assetId = snapshot->id();
          scIn.revision = snapshot->revision();
          scIn.valueDomain = QStringLiteral( "raster" );
          if ( !lineage.inputs.contains( scIn ) )
            lineage.inputs.append( scIn );
          resolved = true;
        }
      }
      if ( !resolved && !assetIdStr.isEmpty() )
      {
        const auto aid = AssetId::fromString( assetIdStr );
        if ( aid )
        {
          const auto snapshot = dataManager->asset( *aid );
          if ( snapshot )
          {
            DerivationInput scIn;
            scIn.assetId = snapshot->id();
            scIn.revision = snapshot->revision();
            scIn.valueDomain = QStringLiteral( "raster" );
            if ( !lineage.inputs.contains( scIn ) )
              lineage.inputs.append( scIn );
            resolved = true;
          }
        }
      }
      if ( !resolved && !scPath.isEmpty() )
      {
        if ( !lineage.unresolvedPaths.contains( scPath ) )
          lineage.unresolvedPaths.append( scPath );
      }
    }
  }

  return lineage;
}

} // namespace sicnu::data
