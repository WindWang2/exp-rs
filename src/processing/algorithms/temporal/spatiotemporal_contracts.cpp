// src/processing/algorithms/temporal/spatiotemporal_contracts.cpp
#include "spatiotemporal_contracts.h"

#include <QCryptographicHash>
#include <QHash>

namespace sicnu::temporal
{

Modality modalityFromString( const QString &token )
{
  const QString t = token.trimmed().toLower();
  if ( t.isEmpty() || t == QLatin1String( "unknown" ) || t == QLatin1String( "multimodal" ) )
    return Modality::Unknown;
  if ( t == QLatin1String( "optical" ) )
    return Modality::Optical;
  if ( t == QLatin1String( "sar" ) )
    return Modality::Sar;
  if ( t == QLatin1String( "dem" ) || t == QLatin1String( "elevation" ) )
    return Modality::Dem;
  if ( t == QLatin1String( "auxiliary" ) || t == QLatin1String( "aux" ) )
    return Modality::Auxiliary;
  if ( t == QLatin1String( "model" ) || t == QLatin1String( "model_derived" ) ||
       t == QLatin1String( "model-derived" ) )
    return Modality::ModelDerived;
  return Modality::Unknown;
}

QString modalityToString( Modality modality )
{
  switch ( modality )
  {
    case Modality::Optical:
      return QStringLiteral( "optical" );
    case Modality::Sar:
      return QStringLiteral( "sar" );
    case Modality::Dem:
      return QStringLiteral( "dem" );
    case Modality::Auxiliary:
      return QStringLiteral( "auxiliary" );
    case Modality::ModelDerived:
      return QStringLiteral( "model_derived" );
    case Modality::Unknown:
      break;
  }
  return QStringLiteral( "unknown" );
}

QString normalizePolarization( const QString &token )
{
  const QString t = token.trimmed().toUpper();
  if ( t == QLatin1String( "CO-POL" ) || t == QLatin1String( "COPOL" ) )
    return QStringLiteral( "co-pol" );
  if ( t == QLatin1String( "CROSS-POL" ) || t == QLatin1String( "CROSSPOL" ) )
    return QStringLiteral( "cross-pol" );
  return t;
}

QStringList normalizePolarizations( const QStringList &tokens )
{
  QStringList out;
  for ( const QString &t : tokens )
  {
    const QString n = normalizePolarization( t );
    if ( !n.isEmpty() && !out.contains( n ) )
      out << n;
  }
  return out;
}

Modality inferModalityFromClues( const QString &platform,
                                 const QString &sensor,
                                 const QString &radiometricState,
                                 const QStringList &bandRoles )
{
  const QString combined = QStringLiteral( "%1 %2" ).arg( platform, sensor ).toUpper();
  // SAR platforms / sensors first: their band roles overlap with nothing else.
  if ( combined.contains( QLatin1String( "SENTINEL-1" ) ) || combined.contains( QLatin1String( "SENTINEL 1" ) ) ||
       combined.contains( QLatin1String( "S1A" ) ) || combined.contains( QLatin1String( "S1B" ) ) ||
       combined.contains( QLatin1String( "S1C" ) ) || combined.contains( QLatin1String( "ALOS" ) ) ||
       combined.contains( QLatin1String( "TERRASAR" ) ) || combined.contains( QLatin1String( "TANDEM-X" ) ) ||
       combined.contains( QLatin1String( "RADARSAT" ) ) || combined.contains( QLatin1String( "SENTINEL-1" ) ) ||
       combined.contains( QLatin1String( "SAR" ) ) || combined.contains( QLatin1String( "ERS-1" ) ) ||
       combined.contains( QLatin1String( "ERS-2" ) ) || combined.contains( QLatin1String( "ENVISAT" ) ) ||
       combined.contains( QLatin1String( "GAOFEN-3" ) ) || combined.contains( QLatin1String( "GF-3" ) ) ||
       combined.contains( QLatin1String( "LT-1" ) ) )
    return Modality::Sar;
  if ( combined.contains( QLatin1String( "DEM" ) ) || combined.contains( QLatin1String( "COPERNICUS" ) ) ||
       combined.contains( QLatin1String( "SRTM" ) ) || combined.contains( QLatin1String( "ASTER GDEM" ) ) ||
       combined.contains( QLatin1String( "ALOS WORLD 3D" ) ) || combined.contains( QLatin1String( "AW3D" ) ) ||
       combined.contains( QLatin1String( "NASADEM" ) ) )
    return Modality::Dem;
  for ( const QString &role : bandRoles )
  {
    const QString r = role.toLower();
    if ( r == QLatin1String( "vv" ) || r == QLatin1String( "vh" ) || r == QLatin1String( "hh" ) ||
         r == QLatin1String( "hv" ) )
      return Modality::Sar;
    if ( r == QLatin1String( "elevation" ) )
      return Modality::Dem;
  }
  const QString state = radiometricState.toLower();
  if ( state.contains( QLatin1String( "sigma" ) ) || state.contains( QLatin1String( "gamma" ) ) ||
       state.contains( QLatin1String( "beta" ) ) )
    return Modality::Sar;
  if ( state.contains( QLatin1String( "elevation" ) ) )
    return Modality::Dem;
  if ( state == QLatin1String( "toa_reflectance" ) || state == QLatin1String( "boa_reflectance" ) ||
       state == QLatin1String( "surface_reflectance" ) || state == QLatin1String( "brightness_temperature" ) ||
       state == QLatin1String( "radiance" ) )
    return Modality::Optical;
  return Modality::Unknown;
}

static QString stablePathDigest( const QString &path )
{
  const QByteArray digest =
    QCryptographicHash::hash( path.toUtf8(), QCryptographicHash::Sha1 ).toHex();
  return QString::fromLatin1( digest.left( 16 ) );
}

ObservationContract ObservationContract::fromSceneRef( const TemporalSceneRef &scene,
                                                       const QString &collectionId,
                                                       const QString &collectionRevision )
{
  ObservationContract c;
  c.path = scene.path;
  c.assetId = scene.assetId;
  c.assetRevision = scene.assetRevision;
  c.observationId = scene.assetId.isEmpty() ? stablePathDigest( scene.path ) : scene.assetId;
  c.collectionId = collectionId;
  c.collectionRevision = collectionRevision;
  c.sensor = scene.sensor;
  c.platform = scene.platform;
  c.processingLevel = scene.processingLevel;
  c.time = scene.time;
  c.bandOverrides = scene.bandOverrides;
  c.bandRoles = scene.bandRoles;
  for ( auto it = scene.bandOverrides.cbegin(); it != scene.bandOverrides.cend(); ++it )
  {
    if ( !c.bandRoles.contains( it->first ) )
      c.bandRoles << it->first;
  }
  c.polarizations = normalizePolarizations( scene.polarizations );
  c.radiometricState = scene.radiometricState;
  c.resolutionMeters = scene.resolutionMeters;
  c.cloudCoverPercent = scene.cloudCoverPercent;
  c.qualityBand = scene.qualityBand;
  c.maskBand = scene.maskBand;
  c.originalIndex = scene.originalIndex;
  c.modality = modalityFromString( scene.modality );
  if ( c.modality == Modality::Unknown )
    c.modality = inferModalityFromClues( scene.platform, scene.sensor, scene.radiometricState,
                                         c.bandRoles );
  return c;
}

QVector<ObservationContract> contractsOf( const TemporalCollection &collection )
{
  QVector<ObservationContract> out;
  out.reserve( collection.scenes().size() );
  for ( const TemporalSceneRef &scene : collection.scenes() )
    out.push_back( ObservationContract::fromSceneRef( scene ) );
  return out;
}

QStringList distinctModalities( const QVector<ObservationContract> &contracts )
{
  QHash<QString, int> seen;
  for ( const ObservationContract &c : contracts )
  {
    const QString m = modalityToString( c.modality );
    seen[m] += 1;
  }
  QStringList ordered;
  const char *kVocabulary[] = { "optical", "sar", "dem", "auxiliary", "model_derived" };
  for ( const char *token : kVocabulary )
  {
    if ( seen.contains( QLatin1String( token ) ) )
      ordered << QLatin1String( token );
  }
  if ( ordered.isEmpty() && seen.contains( QLatin1String( "unknown" ) ) )
    ordered << QStringLiteral( "unknown" );
  return ordered;
}

} // namespace sicnu::temporal
