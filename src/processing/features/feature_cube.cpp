// src/processing/features/feature_cube.cpp
#include "feature_cube.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <sstream>
#include <QJsonDocument>
#include <algorithm>
#include <limits>

namespace sicnu::features
{

namespace
{
constexpr const char *kCubeKey = "SICNU_FEATURE_CUBE";
constexpr const char *kBandIdKey = "SICNU_FEATURE_ID";
constexpr const char *kBandRoleKey = "SICNU_FEATURE_ROLE";
constexpr const char *kBandUnitKey = "SICNU_FEATURE_UNIT";
/// GDAL metadata items live comfortably below 64 KiB; larger contracts spill
/// to the sidecar.
constexpr int kMaxDatasetItemChars = 60000;

Json::Value featureBandToJson( const FeatureBand &b )
{
  Json::Value v( Json::objectValue );
  v["id"] = b.id.toStdString();
  if ( !b.semanticRole.isEmpty() )
    v["role"] = b.semanticRole.toStdString();
  if ( !b.unit.isEmpty() )
    v["unit"] = b.unit.toStdString();
  if ( b.scale != 1.0 )
    v["scale"] = b.scale;
  if ( b.offset != 0.0 )
    v["offset"] = b.offset;
  v["band"] = b.band;
  if ( !b.sourcePath.isEmpty() )
    v["source_path"] = b.sourcePath.toStdString();
  if ( !b.sourceAssetId.isEmpty() )
    v["source_asset_id"] = b.sourceAssetId.toStdString();
  if ( !b.modality.isEmpty() )
    v["modality"] = b.modality.toStdString();
  if ( !b.time.kind.isEmpty() && b.time.kind != QLatin1String( "none" ) )
  {
    Json::Value t( Json::objectValue );
    t["kind"] = b.time.kind.toStdString();
    if ( !b.time.startIso.isEmpty() )
      t["start"] = b.time.startIso.toStdString();
    if ( !b.time.endIso.isEmpty() )
      t["end"] = b.time.endIso.toStdString();
    v["time"] = t;
  }
  if ( !std::isnan( b.nodata ) )
    v["nodata"] = b.nodata;
  return v;
}

bool featureBandFromJson( const Json::Value &v, int index, FeatureBand *out, QString *error )
{
  if ( !v.isObject() || !v.isMember( "id" ) || !v["id"].isString() )
  {
    if ( error )
      *error = QStringLiteral( "feature band %1 needs a string 'id'" ).arg( index );
    return false;
  }
  out->id = QString::fromStdString( v["id"].asString() );
  out->band = v.isMember( "band" ) && v["band"].isNumeric() ? v["band"].asInt() : index + 1;
  if ( v.isMember( "role" ) && v["role"].isString() )
    out->semanticRole = QString::fromStdString( v["role"].asString() );
  if ( !out->semanticRole.isEmpty() && out->semanticRole == out->id )
  {
    // id defaults to role; keep both consistent without duplicating data.
  }
  if ( v.isMember( "unit" ) && v["unit"].isString() )
    out->unit = QString::fromStdString( v["unit"].asString() );
  if ( v.isMember( "scale" ) && v["scale"].isNumeric() )
    out->scale = v["scale"].asDouble();
  if ( v.isMember( "offset" ) && v["offset"].isNumeric() )
    out->offset = v["offset"].asDouble();
  if ( v.isMember( "source_path" ) && v["source_path"].isString() )
    out->sourcePath = QString::fromStdString( v["source_path"].asString() );
  if ( v.isMember( "source_asset_id" ) && v["source_asset_id"].isString() )
    out->sourceAssetId = QString::fromStdString( v["source_asset_id"].asString() );
  if ( v.isMember( "modality" ) && v["modality"].isString() )
    out->modality = QString::fromStdString( v["modality"].asString() );
  if ( v.isMember( "time" ) && v["time"].isObject() )
  {
    const Json::Value &t = v["time"];
    if ( t.isMember( "kind" ) && t["kind"].isString() )
      out->time.kind = QString::fromStdString( t["kind"].asString() );
    if ( t.isMember( "start" ) && t["start"].isString() )
      out->time.startIso = QString::fromStdString( t["start"].asString() );
    if ( t.isMember( "end" ) && t["end"].isString() )
      out->time.endIso = QString::fromStdString( t["end"].asString() );
  }
  if ( v.isMember( "nodata" ) && v["nodata"].isNumeric() )
    out->nodata = v["nodata"].asDouble();
  return true;
}

bool writeSidecar( const QString &sidecarPath, const QString &json )
{
  QFile f( sidecarPath );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  return f.write( json.toUtf8() ) >= 0;
}
} // namespace

Json::Value FeatureCubeContract::toJson() const
{
  Json::Value v( Json::objectValue );
  v["version"] = version;
  if ( !featureId.isEmpty() )
    v["feature_id"] = featureId.toStdString();
  if ( !generator.isEmpty() )
    v["generator"] = generator.toStdString();
  Json::Value bandsJson( Json::arrayValue );
  for ( const FeatureBand &b : bands )
    bandsJson.append( featureBandToJson( b ) );
  v["bands"] = bandsJson;
  if ( normalization.isObject() && !normalization.empty() )
    v["normalization"] = normalization;
  return v;
}

bool FeatureCubeContract::fromJson( const Json::Value &v, FeatureCubeContract *out,
                                    QString *error )
{
  if ( !v.isObject() || !v.isMember( "version" ) || v["version"].asInt() != 1 )
  {
    if ( error )
      *error = QStringLiteral( "unsupported feature cube contract version" );
    return false;
  }
  out->bands.clear();
  if ( v.isMember( "feature_id" ) && v["feature_id"].isString() )
    out->featureId = QString::fromStdString( v["feature_id"].asString() );
  if ( v.isMember( "generator" ) && v["generator"].isString() )
    out->generator = QString::fromStdString( v["generator"].asString() );
  if ( v.isMember( "normalization" ) && v["normalization"].isObject() )
    out->normalization = v["normalization"];
  if ( !v.isMember( "bands" ) || !v["bands"].isArray() || v["bands"].empty() )
  {
    if ( error )
      *error = QStringLiteral( "feature cube contract needs a non-empty 'bands' array" );
    return false;
  }
  int index = 0;
  QStringList ids;
  for ( const Json::Value &b : v["bands"] )
  {
    FeatureBand band;
    if ( !featureBandFromJson( b, index, &band, error ) )
      return false;
    if ( ids.contains( band.id ) )
    {
      if ( error )
        *error = QStringLiteral( "duplicate feature id '%1'" ).arg( band.id );
      return false;
    }
    ids << band.id;
    out->bands.push_back( band );
    ++index;
  }
  return true;
}

bool writeFeatureCubeMetadata( void *datasetHandle, const FeatureCubeContract &contract,
                               const QString &sidecarPath )
{
  GDALDatasetH ds = static_cast<GDALDatasetH>( datasetHandle );
  if ( !ds )
    return false;
  const QString json = QString::fromStdString(
    Json::writeString( Json::StreamWriterBuilder(), contract.toJson() ) );

  if ( json.size() <= kMaxDatasetItemChars )
  {
    GDALSetMetadataItem( ds, kCubeKey, json.toUtf8().constData(), nullptr );
  }
  else if ( !sidecarPath.isEmpty() )
  {
    if ( !writeSidecar( sidecarPath, json ) )
      return false;
    Json::Value stub( Json::objectValue );
    stub["version"] = 1;
    stub["sidecar"] = sidecarPath.toStdString();
    stub["band_count"] = static_cast<Json::ArrayIndex>( contract.bands.size() );
    const QString stubJson = QString::fromStdString(
      Json::writeString( Json::StreamWriterBuilder(), stub ) );
    GDALSetMetadataItem( ds, kCubeKey, stubJson.toUtf8().constData(), nullptr );
  }
  else
  {
    return false;
  }

  for ( const FeatureBand &b : contract.bands )
  {
    if ( b.band < 1 || b.band > contract.bands.size() )
      continue;
    GDALRasterBandH band = GDALGetRasterBand( ds, b.band );
    if ( !band )
      continue;
    GDALSetMetadataItem( band, kBandIdKey, b.id.toUtf8().constData(), nullptr );
    if ( !b.semanticRole.isEmpty() )
      GDALSetMetadataItem( band, kBandRoleKey, b.semanticRole.toUtf8().constData(), nullptr );
    if ( !b.unit.isEmpty() )
      GDALSetMetadataItem( band, kBandUnitKey, b.unit.toUtf8().constData(), nullptr );
  }
  return true;
}

bool readFeatureCubeMetadata( const QString &rasterPath, FeatureCubeContract *out,
                              QString *error )
{
  GdalDatasetWrapper ds;
  if ( !ds.open( rasterPath ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot open raster %1" ).arg( rasterPath );
    return false;
  }
  const char *raw = GDALGetMetadataItem( static_cast<GDALDatasetH>( ds.dataset() ), kCubeKey, nullptr );
  if ( !raw )
    return false;
  const QString payload = QString::fromUtf8( raw );

  const QByteArray utf8 = payload.toUtf8();
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::string errs;
  std::istringstream stream( utf8.toStdString() );
  if ( !Json::parseFromStream( builder, stream, &parsed, &errs ) )
  {
    if ( error )
      *error = QStringLiteral( "invalid feature cube metadata: %1" )
                 .arg( QString::fromStdString( errs ) );
    return false;
  }
  // Sidecar indirection.
  if ( parsed.isObject() && parsed.isMember( "sidecar" ) && parsed["sidecar"].isString() )
  {
    const QString sidecarPath = QString::fromStdString( parsed["sidecar"].asString() );
    QFile f( sidecarPath );
    if ( !f.open( QIODevice::ReadOnly ) )
    {
      if ( error )
        *error = QStringLiteral( "feature cube sidecar unreadable: %1" ).arg( sidecarPath );
      return false;
    }
    const QByteArray sidecarBytes = f.readAll();
    std::istringstream sidecarStream( sidecarBytes.toStdString() );
    if ( !Json::parseFromStream( builder, sidecarStream, &parsed, &errs ) )
    {
      if ( error )
        *error = QStringLiteral( "invalid feature cube sidecar: %1" )
                   .arg( QString::fromStdString( errs ) );
      return false;
    }
  }
  return FeatureCubeContract::fromJson( parsed, out, error );
}

bool isFeatureCube( const QString &rasterPath )
{
  FeatureCubeContract c;
  return readFeatureCubeMetadata( rasterPath, &c );
}

ModelInputMatch matchesModelInput( const FeatureCubeContract &cube,
                                   const QStringList &requiredBandRoles,
                                   int expectedBandCount, const QString &expectedModality )
{
  ModelInputMatch m;
  QStringList roles;
  for ( const FeatureBand &b : cube.bands )
  {
    if ( !b.semanticRole.isEmpty() )
      roles << b.semanticRole.toLower();
  }

  for ( const QString &required : requiredBandRoles )
  {
    if ( !roles.contains( required.toLower() ) )
    {
      ++m.missingRoles;
      m.problems << QStringLiteral( "cube lacks required role '%1'" ).arg( required );
    }
  }
  if ( expectedBandCount > 0 )
  {
    m.bandCountDelta = cube.bands.size() - expectedBandCount;
    if ( m.bandCountDelta != 0 )
    {
      m.problems << QStringLiteral( "cube has %1 bands, model input expects %2" )
                      .arg( cube.bands.size() )
                      .arg( expectedBandCount );
    }
  }
  if ( !expectedModality.isEmpty() )
  {
    QStringList modalities;
    for ( const FeatureBand &b : cube.bands )
    {
      if ( !b.modality.isEmpty() && !modalities.contains( b.modality ) )
        modalities << b.modality;
    }
    if ( !modalities.contains( expectedModality ) && !modalities.isEmpty() )
      m.problems << QStringLiteral( "cube modalities (%1) do not include model domain '%2'" )
                      .arg( modalities.join( QLatin1String( "/" ) ), expectedModality );
  }
  m.ok = m.problems.isEmpty();
  return m;
}

} // namespace sicnu::features
