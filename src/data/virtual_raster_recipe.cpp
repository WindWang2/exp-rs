#include "virtual_raster_recipe.h"

#include <QJsonArray>

namespace sicnu::data
{

namespace
{

const QString kInputs = QStringLiteral( "inputs" );
const QString kAssetId = QStringLiteral( "assetId" );
const QString kBandNumber = QStringLiteral( "bandNumber" );
const QString kTargetCrs = QStringLiteral( "targetCrs" );
const QString kTargetResolutionX = QStringLiteral( "targetResolutionX" );
const QString kTargetResolutionY = QStringLiteral( "targetResolutionY" );
const QString kExtentPolicy = QStringLiteral( "extentPolicy" );
const QString kResampling = QStringLiteral( "resampling" );
const QString kNoDataPolicy = QStringLiteral( "noDataPolicy" );
const QString kNoDataFillValue = QStringLiteral( "noDataFillValue" );

QString extentPolicyToString( ExtentPolicy policy )
{
  switch ( policy )
  {
    case ExtentPolicy::Intersection:
      return QStringLiteral( "intersection" );
    case ExtentPolicy::Union:
      return QStringLiteral( "union" );
  }
  Q_UNREACHABLE();
}

Result<ExtentPolicy> extentPolicyFromString( const QString &text )
{
  if ( text == QLatin1String( "intersection" ) )
    return Result<ExtentPolicy>::success( ExtentPolicy::Intersection );
  if ( text == QLatin1String( "union" ) )
    return Result<ExtentPolicy>::success( ExtentPolicy::Union );
  return Result<ExtentPolicy>::failure(
    { QStringLiteral( "recipe.invalid" ),
      QStringLiteral( "Unknown extent policy: %1" ).arg( text ) } );
}

QString resamplingToString( ResamplingMethod method )
{
  switch ( method )
  {
    case ResamplingMethod::NearestNeighbour:
      return QStringLiteral( "nearest" );
    case ResamplingMethod::Bilinear:
      return QStringLiteral( "bilinear" );
    case ResamplingMethod::Cubic:
      return QStringLiteral( "cubic" );
  }
  Q_UNREACHABLE();
}

Result<ResamplingMethod> resamplingFromString( const QString &text )
{
  if ( text == QLatin1String( "nearest" ) )
    return Result<ResamplingMethod>::success( ResamplingMethod::NearestNeighbour );
  if ( text == QLatin1String( "bilinear" ) )
    return Result<ResamplingMethod>::success( ResamplingMethod::Bilinear );
  if ( text == QLatin1String( "cubic" ) )
    return Result<ResamplingMethod>::success( ResamplingMethod::Cubic );
  return Result<ResamplingMethod>::failure(
    { QStringLiteral( "recipe.invalid" ),
      QStringLiteral( "Unknown resampling method: %1" ).arg( text ) } );
}

QString noDataPolicyToString( NoDataPolicy policy )
{
  switch ( policy )
  {
    case NoDataPolicy::Preserve:
      return QStringLiteral( "preserve" );
    case NoDataPolicy::FillValue:
      return QStringLiteral( "fill" );
  }
  Q_UNREACHABLE();
}

Result<NoDataPolicy> noDataPolicyFromString( const QString &text )
{
  if ( text == QLatin1String( "preserve" ) )
    return Result<NoDataPolicy>::success( NoDataPolicy::Preserve );
  if ( text == QLatin1String( "fill" ) )
    return Result<NoDataPolicy>::success( NoDataPolicy::FillValue );
  return Result<NoDataPolicy>::failure(
    { QStringLiteral( "recipe.invalid" ),
      QStringLiteral( "Unknown NoData policy: %1" ).arg( text ) } );
}

QJsonObject bandRefToJson( const BandRef &bandRef )
{
  QJsonObject json;
  json.insert( kAssetId,
               bandRef.asset.isNull() ? QString() : bandRef.asset.toString() );
  json.insert( kBandNumber, bandRef.bandNumber );
  return json;
}

Result<BandRef> bandRefFromJson( const QJsonObject &json )
{
  BandRef bandRef;
  const QString assetText = json.value( kAssetId ).toString();
  const std::optional<AssetId> asset = AssetId::fromString( assetText );
  if ( !asset )
  {
    return Result<BandRef>::failure(
      { QStringLiteral( "recipe.invalid" ),
        QStringLiteral( "assetId is not a valid Asset ID: %1" ).arg( assetText ) } );
  }
  bandRef.asset = *asset;
  // An absent bandNumber defaults to 1 (the struct default); an explicitly
  // present but invalid value (< 1) is rejected below.
  bandRef.bandNumber = json.value( kBandNumber ).toInt( 1 );
  if ( bandRef.bandNumber < 1 )
  {
    return Result<BandRef>::failure(
      { QStringLiteral( "recipe.invalid" ),
        QStringLiteral( "bandNumber must be a 1-based band index, got %1" )
          .arg( bandRef.bandNumber ) } );
  }
  return Result<BandRef>::success( bandRef );
}

} // namespace

QJsonObject VirtualRasterRecipe::toJson() const
{
  QJsonObject json;

  QJsonArray inputsJson;
  for ( const BandRef &input : inputs )
    inputsJson.append( bandRefToJson( input ) );
  json.insert( kInputs, inputsJson );

  json.insert( kTargetCrs, targetCrs );
  json.insert( kTargetResolutionX, targetResolutionX );
  json.insert( kTargetResolutionY, targetResolutionY );
  json.insert( kExtentPolicy, extentPolicyToString( extentPolicy ) );
  json.insert( kResampling, resamplingToString( resampling ) );
  json.insert( kNoDataPolicy, noDataPolicyToString( noDataPolicy ) );
  json.insert( kNoDataFillValue, noDataFillValue );
  return json;
}

Result<VirtualRasterRecipe> VirtualRasterRecipe::fromJson( const QJsonObject &json )
{
  VirtualRasterRecipe recipe;

  const QJsonArray inputsJson = json.value( kInputs ).toArray();
  if ( inputsJson.isEmpty() )
  {
    return Result<VirtualRasterRecipe>::failure(
      { QStringLiteral( "recipe.invalid" ),
        QStringLiteral( "A virtual raster recipe requires at least one input band" ) } );
  }
  for ( const QJsonValue &inputValue : inputsJson )
  {
    const Result<BandRef> input = bandRefFromJson( inputValue.toObject() );
    if ( !input )
      return Result<VirtualRasterRecipe>::failure( input.diagnostics() );
    recipe.inputs.append( input.value() );
  }

  recipe.targetCrs = json.value( kTargetCrs ).toString();
  recipe.targetResolutionX = json.value( kTargetResolutionX ).toDouble( 0.0 );
  recipe.targetResolutionY = json.value( kTargetResolutionY ).toDouble( 0.0 );

  // Absent enum keys fall back to the struct defaults (mirroring
  // DerivationRecord's leniency for absent scalars); an explicitly present but
  // unknown spelling is rejected.
  if ( json.contains( kExtentPolicy ) )
  {
    const Result<ExtentPolicy> extentPolicy =
      extentPolicyFromString( json.value( kExtentPolicy ).toString() );
    if ( !extentPolicy )
      return Result<VirtualRasterRecipe>::failure( extentPolicy.diagnostics() );
    recipe.extentPolicy = extentPolicy.value();
  }

  if ( json.contains( kResampling ) )
  {
    const Result<ResamplingMethod> resampling =
      resamplingFromString( json.value( kResampling ).toString() );
    if ( !resampling )
      return Result<VirtualRasterRecipe>::failure( resampling.diagnostics() );
    recipe.resampling = resampling.value();
  }

  if ( json.contains( kNoDataPolicy ) )
  {
    const Result<NoDataPolicy> noDataPolicy =
      noDataPolicyFromString( json.value( kNoDataPolicy ).toString() );
    if ( !noDataPolicy )
      return Result<VirtualRasterRecipe>::failure( noDataPolicy.diagnostics() );
    recipe.noDataPolicy = noDataPolicy.value();
  }

  recipe.noDataFillValue = json.value( kNoDataFillValue ).toDouble( 0.0 );
  return Result<VirtualRasterRecipe>::success( recipe );
}

} // namespace sicnu::data
