// src/processing/algorithms/temporal/temporal_collection.cpp
#include "temporal_collection.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <gdal.h>

#include <algorithm>
#include <memory>
#include <sstream>

namespace sicnu::temporal
{

namespace
{
constexpr int kDescriptorVersion = 1;

QString datasetMetadataItem( const GdalDatasetWrapper &ds, const char *key )
{
  if ( !ds.isValid() )
    return {};
  const char *value = GDALGetMetadataItem( static_cast<GDALDatasetH>( ds.dataset() ),
                                           key, nullptr );
  return value ? QString::fromUtf8( value ) : QString();
}
} // namespace

Json::Value TemporalSceneRef::toJson() const
{
  Json::Value v( Json::objectValue );
  v["path"] = path.toStdString();
  if ( !assetId.isEmpty() )
    v["asset_id"] = assetId.toStdString();
  if ( !assetRevision.isEmpty() )
    v["asset_revision"] = assetRevision.toStdString();
  if ( time.valid )
  {
    v["time"] = time.iso.toStdString();
    v["time_precision"] = time.precision == TimePrecision::DateTime ? "datetime" : "date";
  }
  v["time_source"] = timeSource.toStdString();
  if ( !platform.isEmpty() )
    v["platform"] = platform.toStdString();
  if ( !processingLevel.isEmpty() )
    v["processing_level"] = processingLevel.toStdString();
  if ( !bandOverrides.empty() )
  {
    Json::Value bands( Json::objectValue );
    for ( const auto &kv : bandOverrides )
      bands[kv.first.toStdString()] = kv.second;
    v["bands"] = bands;
  }
  if ( qualityBand > 0 )
    v["quality_band"] = qualityBand;
  if ( maskBand > 0 )
    v["mask_band"] = maskBand;
  // Multimodal observation contract (goal §11): optional additive fields,
  // serialized only when claimed so legacy descriptors stay byte-stable.
  if ( !modality.isEmpty() )
    v["modality"] = modality.toStdString();
  if ( !sensor.isEmpty() )
    v["sensor"] = sensor.toStdString();
  if ( !bandRoles.isEmpty() )
  {
    Json::Value roles( Json::arrayValue );
    for ( const QString &role : bandRoles )
      roles.append( role.toStdString() );
    v["band_roles"] = roles;
  }
  if ( !polarizations.isEmpty() )
  {
    Json::Value pol( Json::arrayValue );
    for ( const QString &p : polarizations )
      pol.append( p.toStdString() );
    v["polarizations"] = pol;
  }
  if ( resolutionMeters > 0.0 )
    v["resolution_m"] = resolutionMeters;
  if ( !radiometricState.isEmpty() )
    v["radiometric_state"] = radiometricState.toStdString();
  if ( cloudCoverPercent >= 0.0 )
    v["cloud_cover_percent"] = cloudCoverPercent;
  v["index"] = originalIndex;
  return v;
}

TemporalSceneRef TemporalSceneRef::fromJson( const Json::Value &v, QString *error )
{
  TemporalSceneRef s;
  if ( !v.isObject() || !v.isMember( "path" ) || !v["path"].isString() )
  {
    if ( error )
      *error = QStringLiteral( "scene entry needs a string 'path'" );
    return s;
  }
  auto requireStr = [&]( const char *key, QString *out ) {
    if ( !v.isMember( key ) )
      return true;
    if ( !v[key].isString() )
    {
      if ( error )
        *error = QStringLiteral( "scene '%1' must be a string" ).arg( QLatin1String( key ) );
      return false;
    }
    *out = QString::fromStdString( v[key].asString() );
    return true;
  };
  auto requireInt = [&]( const char *key, int *out ) {
    if ( !v.isMember( key ) )
      return true;
    if ( !v[key].isNumeric() )
    {
      if ( error )
        *error = QStringLiteral( "scene '%1' must be an integer" ).arg( QLatin1String( key ) );
      return false;
    }
    *out = v[key].asInt();
    return true;
  };

  s.path = QString::fromStdString( v["path"].asString() );
  if ( s.path.isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "scene 'path' must be non-empty" );
    return s;
  }
  if ( !requireStr( "asset_id", &s.assetId ) || !requireStr( "asset_revision", &s.assetRevision ) ||
       !requireStr( "time_source", &s.timeSource ) || !requireStr( "platform", &s.platform ) ||
       !requireStr( "processing_level", &s.processingLevel ) ||
       !requireInt( "quality_band", &s.qualityBand ) || !requireInt( "mask_band", &s.maskBand ) ||
       !requireInt( "index", &s.originalIndex ) )
    return s;
  if ( v.isMember( "time" ) )
  {
    if ( !v["time"].isString() )
    {
      if ( error )
        *error = QStringLiteral( "scene 'time' must be a string" );
      return s;
    }
    s.time = parseAcquisitionTime( QString::fromStdString( v["time"].asString() ) );
    s.timeSource = QStringLiteral( "descriptor" );
  }
  if ( v.isMember( "bands" ) )
  {
    if ( !v["bands"].isObject() )
    {
      if ( error )
        *error = QStringLiteral( "scene 'bands' must be an object of role->band" );
      return s;
    }
    for ( auto it = v["bands"].begin(); it != v["bands"].end(); ++it )
    {
      if ( !( *it ).isNumeric() )
      {
        if ( error )
          *error = QStringLiteral( "scene 'bands.%1' must be an integer" )
                       .arg( QString::fromStdString( it.name() ) );
        return s;
      }
      s.bandOverrides[QString::fromStdString( it.name() )] = ( *it ).asInt();
    }
  }
  // Multimodal observation contract (goal §11): optional additive fields.
  // Unknown keys are ignored (forward compatibility); known keys are
  // type-checked so a malformed contract fails loudly instead of silently
  // degrading to the optical default.
  if ( !requireStr( "modality", &s.modality ) || !requireStr( "sensor", &s.sensor )
       || !requireStr( "radiometric_state", &s.radiometricState ) )
    return s;
  const auto requireStrList = [&]( const char *key, QStringList *out ) {
    if ( !v.isMember( key ) )
      return true;
    if ( !v[key].isArray() )
    {
      if ( error )
        *error = QStringLiteral( "scene '%1' must be an array of strings" )
                     .arg( QLatin1String( key ) );
      return false;
    }
    for ( const auto &item : v[key] )
    {
      if ( !item.isString() )
      {
        if ( error )
          *error = QStringLiteral( "scene '%1' must be an array of strings" )
                       .arg( QLatin1String( key ) );
        return false;
      }
      out->append( QString::fromStdString( item.asString() ) );
    }
    return true;
  };
  if ( !requireStrList( "band_roles", &s.bandRoles )
       || !requireStrList( "polarizations", &s.polarizations ) )
    return s;
  if ( v.isMember( "resolution_m" ) )
  {
    if ( !v["resolution_m"].isNumeric() )
    {
      if ( error )
        *error = QStringLiteral( "scene 'resolution_m' must be a number" );
      return s;
    }
    s.resolutionMeters = v["resolution_m"].asDouble();
  }
  if ( v.isMember( "cloud_cover_percent" ) )
  {
    if ( !v["cloud_cover_percent"].isNumeric() )
    {
      if ( error )
        *error = QStringLiteral( "scene 'cloud_cover_percent' must be a number" );
      return s;
    }
    s.cloudCoverPercent = v["cloud_cover_percent"].asDouble();
  }
  return s;
}

bool inspectScene( const QString &path, const QString &explicitTime,
                   TemporalSceneRef *out, QString *error )
{
  ensureGdalInit();
  GdalDatasetWrapper ds;
  if ( !ds.open( path ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot open raster: %1" ).arg( path );
    return false;
  }

  out->path = path;
  out->platform = datasetMetadataItem( ds, "SICNU_SPACECRAFT" );
  out->processingLevel = datasetMetadataItem( ds, "SICNU_PROCESSING_LEVEL" );

  if ( !explicitTime.isEmpty() )
  {
    out->time = parseAcquisitionTime( explicitTime );
    out->timeSource = QStringLiteral( "explicit" );
    if ( !out->time.valid && error )
      *error = QStringLiteral( "invalid explicit time '%1' for %2" ).arg( explicitTime, path );
    return out->time.valid;
  }

  const QString meta = datasetMetadataItem( ds, "SICNU_ACQUISITION_DATE" );
  if ( !meta.isEmpty() )
  {
    out->time = parseAcquisitionTime( meta );
    if ( out->time.valid )
    {
      out->timeSource = QStringLiteral( "metadata" );
      return true;
    }
  }

  out->time = timeFromFilename( path );
  if ( out->time.valid )
  {
    out->timeSource = QStringLiteral( "filename" );
    return true;
  }
  out->timeSource = QString();
  return true; // opened fine; time missing is a preflight issue, not an open failure
}

TemporalCollection TemporalCollection::fromScenePaths( const QStringList &paths,
                                                       const QStringList &explicitTimes,
                                                       const QString &name )
{
  TemporalCollection c;
  c.m_name = name;
  c.m_scenes.reserve( paths.size() );
  for ( int i = 0; i < paths.size(); ++i )
  {
    TemporalSceneRef s;
    const QString explicitTime = i < explicitTimes.size() ? explicitTimes.at( i ) : QString();
    QString err;
    inspectScene( paths.at( i ), explicitTime, &s, &err );
    s.originalIndex = i;
    c.m_scenes.push_back( std::move( s ) );
  }
  c.sortScenes();
  return c;
}

void TemporalCollection::sortScenes()
{
  std::stable_sort( m_scenes.begin(), m_scenes.end(),
                    []( const TemporalSceneRef &a, const TemporalSceneRef &b ) {
                      if ( a.time.valid != b.time.valid )
                        return a.time.valid && !b.time.valid; // valid times first
                      if ( a.time.valid && a.time.epochMillis != b.time.epochMillis )
                        return a.time.epochMillis < b.time.epochMillis;
                      return a.originalIndex < b.originalIndex;
                    } );
}

int TemporalCollection::applyDuplicatePolicy( DuplicatePolicy policy, QStringList *droppedPaths )
{
  int dropped = 0;
  if ( policy == DuplicatePolicy::KeepAll )
    return 0;
  QVector<TemporalSceneRef> unique;
  unique.reserve( m_scenes.size() );
  for ( const TemporalSceneRef &s : m_scenes )
  {
    const bool dup = !unique.isEmpty() && s.time.valid &&
                     unique.last().time.valid &&
                     unique.last().time.epochMillis == s.time.epochMillis;
    if ( dup )
    {
      ++dropped;
      if ( droppedPaths )
        droppedPaths->append( s.path );
    }
    else
    {
      unique.push_back( s );
    }
  }
  m_scenes = unique;
  return dropped;
}

QString TemporalCollection::timeRangeStartIso() const
{
  for ( const TemporalSceneRef &s : m_scenes )
    if ( s.time.valid )
      return s.time.iso;
  return {};
}

QString TemporalCollection::timeRangeEndIso() const
{
  for ( int i = m_scenes.size() - 1; i >= 0; --i )
    if ( m_scenes.at( i ).time.valid )
      return m_scenes.at( i ).time.iso;
  return {};
}

Json::Value TemporalCollection::toJson() const
{
  Json::Value v( Json::objectValue );
  v["version"] = kDescriptorVersion;
  v["name"] = m_name.toStdString();
  v["duplicate_policy"] = m_duplicatePolicy == DuplicatePolicy::Reject ? "reject" : "keep_all";
  Json::Value scenes( Json::arrayValue );
  for ( const TemporalSceneRef &s : m_scenes )
    scenes.append( s.toJson() );
  v["scenes"] = scenes;
  return v;
}

bool TemporalCollection::fromJson( const Json::Value &v, TemporalCollection *out, QString *error )
{
  if ( !v.isObject() || !v.isMember( "scenes" ) || !v["scenes"].isArray() )
  {
    if ( error )
      *error = QStringLiteral( "not a temporal collection descriptor (missing scenes array)" );
    return false;
  }
  TemporalCollection c;
  if ( v.isMember( "version" ) && ( !v["version"].isNumeric() ||
                                    v["version"].asInt() > kDescriptorVersion ) )
  {
    if ( error )
      *error = QStringLiteral( "unsupported collection descriptor version %1 (supported: %2)" )
                   .arg( v["version"].isNumeric() ? QString::number( v["version"].asInt() )
                                                  : QStringLiteral( "?" ) )
                   .arg( kDescriptorVersion );
    return false;
  }
  if ( v.isMember( "name" ) && !v["name"].isString() )
  {
    if ( error )
      *error = QStringLiteral( "'name' must be a string" );
    return false;
  }
  if ( v.isMember( "name" ) )
    c.m_name = QString::fromStdString( v["name"].asString() );
  if ( v.isMember( "duplicate_policy" ) && !v["duplicate_policy"].isString() )
  {
    if ( error )
      *error = QStringLiteral( "'duplicate_policy' must be a string" );
    return false;
  }
  if ( v.isMember( "duplicate_policy" ) )
  {
    bool ok = false;
    c.m_duplicatePolicy = duplicatePolicyFromString(
      QString::fromStdString( v["duplicate_policy"].asString() ), &ok );
  }
  for ( const Json::Value &sv : v["scenes"] )
  {
    QString err;
    TemporalSceneRef s = TemporalSceneRef::fromJson( sv, &err );
    if ( !err.isEmpty() || s.path.isEmpty() )
    {
      if ( error )
        *error = err.isEmpty()
                     ? QStringLiteral( "scene entry needs a string 'path'" )
                     : QStringLiteral( "scene %1: %2" ).arg( s.path.isEmpty() ? QString() : s.path ).arg( err );
      return false;
    }
    c.m_scenes.push_back( std::move( s ) );
  }
  c.sortScenes();
  if ( out )
    *out = std::move( c );
  return true;
}

bool TemporalCollection::save( const QString &filePath ) const
{
  QFile f( filePath );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return false;
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  std::ostringstream oss;
  writer->write( toJson(), &oss );
  QTextStream ts( &f );
  ts << QString::fromStdString( oss.str() );
  ts.flush(); // surface ENOSPC now, not in the destructor
  if ( ts.status() != QTextStream::Ok )
  {
    ts.resetStatus();
    f.close();
    QFile::remove( filePath ); // never leave a truncated descriptor behind
    return false;
  }
  return true;
}

bool TemporalCollection::load( const QString &filePath, TemporalCollection *out, QString *error )
{
  QFile f( filePath );
  if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot open collection descriptor: %1" ).arg( filePath );
    return false;
  }
  const QByteArray raw = f.readAll();
  Json::CharReaderBuilder readerBuilder;
  std::unique_ptr<Json::CharReader> reader( readerBuilder.newCharReader() );
  Json::Value v;
  std::string parseErr;
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &v, &parseErr ) )
  {
    if ( error )
      *error = QStringLiteral( "invalid collection JSON: %1" ).arg( QString::fromStdString( parseErr ) );
    return false;
  }
  return fromJson( v, out, error );
}

DuplicatePolicy duplicatePolicyFromString( const QString &token, bool *ok )
{
  const QString t = token.trimmed().toLower();
  if ( t == QLatin1String( "keep_all" ) || t.isEmpty() )
  {
    if ( ok )
      *ok = true;
    return DuplicatePolicy::KeepAll;
  }
  if ( t == QLatin1String( "reject" ) || t == QLatin1String( "reject_duplicate" ) )
  {
    if ( ok )
      *ok = true;
    return DuplicatePolicy::Reject;
  }
  if ( ok )
    *ok = false;
  return DuplicatePolicy::KeepAll;
}

} // namespace sicnu::temporal
