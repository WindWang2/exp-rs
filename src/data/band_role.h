// src/data/band_role.h — semantic band roles for remote-sensing products
#pragma once

#include <QString>
#include <QStringList>

namespace sicnu::data
{

/// Semantic role of a raster band, independent of its 1-based band number.
///
/// Products (Landsat, Sentinel-2, MODIS, ...) assign roles at discovery and
/// stack time; workflows key off the role instead of hard-coded band numbers
/// whenever the role is known. Band numbers remain available as a low-level
/// fallback for rasters without product semantics.
enum class BandRole
{
  Unknown = 0, ///< no role known / not a spectral band (e.g. water vapour)
  Coastal,     ///< coastal/aerosol (OLI B1, S2 B1)
  Blue,
  Green,
  Red,
  RedEdge,     ///< vegetation red edge (S2 B5/B6/B7)
  NIR,
  NarrowNIR,   ///< narrow near-infrared (S2 B8A)
  SWIR1,
  SWIR2,
  Cirrus,      ///< cirrus detection (OLI B9, S2 B10)
  Panchromatic,
  Thermal,
  QA,          ///< quality/auxiliary mask band (QA_PIXEL, QA_RADSAT, MSK_*, AOT, WVP, ...)
  SceneClassification ///< scene-classification layer (S2 SCL)
};

/// Stable lowercase identifier used in on-disk metadata and JSON
/// (e.g. "nir", "red_edge", "scene_classification"). Round-trips with
/// bandRoleFromString().
inline QString bandRoleToString( BandRole role )
{
  switch ( role )
  {
    case BandRole::Coastal: return QStringLiteral( "coastal" );
    case BandRole::Blue: return QStringLiteral( "blue" );
    case BandRole::Green: return QStringLiteral( "green" );
    case BandRole::Red: return QStringLiteral( "red" );
    case BandRole::RedEdge: return QStringLiteral( "red_edge" );
    case BandRole::NIR: return QStringLiteral( "nir" );
    case BandRole::NarrowNIR: return QStringLiteral( "narrow_nir" );
    case BandRole::SWIR1: return QStringLiteral( "swir1" );
    case BandRole::SWIR2: return QStringLiteral( "swir2" );
    case BandRole::Cirrus: return QStringLiteral( "cirrus" );
    case BandRole::Panchromatic: return QStringLiteral( "panchromatic" );
    case BandRole::Thermal: return QStringLiteral( "thermal" );
    case BandRole::QA: return QStringLiteral( "qa" );
    case BandRole::SceneClassification: return QStringLiteral( "scene_classification" );
    case BandRole::Unknown: break;
  }
  return QStringLiteral( "unknown" );
}

/// Inverse of bandRoleToString(); case-insensitive. Returns Unknown for
/// unrecognized identifiers.
inline BandRole bandRoleFromString( const QString &id )
{
  const QString key = id.trimmed().toLower();
  if ( key == QLatin1String( "coastal" ) ) return BandRole::Coastal;
  if ( key == QLatin1String( "blue" ) ) return BandRole::Blue;
  if ( key == QLatin1String( "green" ) ) return BandRole::Green;
  if ( key == QLatin1String( "red" ) ) return BandRole::Red;
  if ( key == QLatin1String( "red_edge" ) ) return BandRole::RedEdge;
  if ( key == QLatin1String( "nir" ) ) return BandRole::NIR;
  if ( key == QLatin1String( "narrow_nir" ) ) return BandRole::NarrowNIR;
  if ( key == QLatin1String( "swir1" ) ) return BandRole::SWIR1;
  if ( key == QLatin1String( "swir2" ) ) return BandRole::SWIR2;
  if ( key == QLatin1String( "cirrus" ) ) return BandRole::Cirrus;
  if ( key == QLatin1String( "panchromatic" ) ) return BandRole::Panchromatic;
  if ( key == QLatin1String( "thermal" ) ) return BandRole::Thermal;
  if ( key == QLatin1String( "qa" ) ) return BandRole::QA;
  if ( key == QLatin1String( "scene_classification" ) ) return BandRole::SceneClassification;
  return BandRole::Unknown;
}

/// Human-readable short label for UI ("Near Infrared (NIR)", "SWIR 1", ...).
inline QString bandRoleDisplayName( BandRole role )
{
  switch ( role )
  {
    case BandRole::Coastal: return QStringLiteral( "Coastal Aerosol" );
    case BandRole::Blue: return QStringLiteral( "Blue" );
    case BandRole::Green: return QStringLiteral( "Green" );
    case BandRole::Red: return QStringLiteral( "Red" );
    case BandRole::RedEdge: return QStringLiteral( "Red Edge" );
    case BandRole::NIR: return QStringLiteral( "NIR" );
    case BandRole::NarrowNIR: return QStringLiteral( "Narrow NIR" );
    case BandRole::SWIR1: return QStringLiteral( "SWIR 1" );
    case BandRole::SWIR2: return QStringLiteral( "SWIR 2" );
    case BandRole::Cirrus: return QStringLiteral( "Cirrus" );
    case BandRole::Panchromatic: return QStringLiteral( "Panchromatic" );
    case BandRole::Thermal: return QStringLiteral( "Thermal" );
    case BandRole::QA: return QStringLiteral( "QA" );
    case BandRole::SceneClassification: return QStringLiteral( "Scene Classification" );
    case BandRole::Unknown: break;
  }
  return {};
}

} // namespace sicnu::data
