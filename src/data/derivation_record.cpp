#include <QFileInfo>
#include <functional>
#include "derivation_record.h"

#include "data_manager.h"

#include <QJsonArray>

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
  json.insert( QStringLiteral( "workflowId" ), workflowId );
  json.insert( QStringLiteral( "workflowRunId" ), workflowRunId );
  json.insert( QStringLiteral( "stepId" ), stepId );
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
  record.workflowId = json.value( QStringLiteral( "workflowId" ) ).toString();
  record.workflowRunId = json.value( QStringLiteral( "workflowRunId" ) ).toString();
  record.stepId = json.value( QStringLiteral( "stepId" ) ).toString();
  return Result<DerivationRecord>::success( record );
}


QStringList findInputPathsInParams( const QVariantMap &params,
                                    const QStringList &excludePaths )
{
  QStringList paths;
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
      if ( trimmed.isEmpty() || trimmed.startsWith( QLatin1Char( '$' ) ) )
        continue;
      if ( excludePaths.contains( trimmed ) )
        continue;
      // Files only: directories and volumes are not lineage inputs, and the
      // isFile gate keeps arbitrary non-path string values from being
      // speculative filesystem probes.
      if ( !paths.contains( trimmed ) && QFileInfo( trimmed ).isFile() )
        paths.append( trimmed );
    }
  };

  // Full scan (#718): lineage edges live on whatever key carries the path
  // ("input", but also dNBR's "postfire" or fusion's "pan"/"ms").
  for ( auto it = params.begin(); it != params.end(); ++it )
    collect( it.value() );
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
    const auto snapshot = dataManager->findByPath( path );
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

} // namespace sicnu::data
