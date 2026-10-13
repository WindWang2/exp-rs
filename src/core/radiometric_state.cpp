// src/core/radiometric_state.cpp — radiometric unit FSM (D13 / ADR 0158)
#include "radiometric_state.h"

#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

namespace exp_radiometric
{
  namespace
  {
    // Lawful forward edges of the FSM (ADR 0158). Stored as a flat table so
    // the transition law reads as data, not control flow.
    struct Transition
    {
      RadiometricUnit from;
      RadiometricUnit to;
    };

    constexpr Transition kLawfulTransitions[] = {
      { RadiometricUnit::DigitalNumber, RadiometricUnit::Radiance },
      { RadiometricUnit::DigitalNumber, RadiometricUnit::ToaReflectance }, // OLI Mρ/Aρ shortcut
      { RadiometricUnit::Radiance,     RadiometricUnit::ToaReflectance },
      { RadiometricUnit::Radiance,     RadiometricUnit::BoaReflectance }, // DOS on radiance
      { RadiometricUnit::Radiance,     RadiometricUnit::BrightnessTemperature },
      { RadiometricUnit::ToaReflectance, RadiometricUnit::BoaReflectance }, // 6S inversion
      // SAR family (#1450): raw counts calibrate to any backscatter state
      // (rs:sar_calibrate → sigma0; backscatter domain conversion accepts
      // sigma0/gamma0/beta0 targets), and the ellipsoid→calibrated→terrain-
      // flattened chain only moves forward. Cross-family transitions into
      // the optical chain stay unlawful.
      { RadiometricUnit::DigitalNumber, RadiometricUnit::Beta0 },
      { RadiometricUnit::DigitalNumber, RadiometricUnit::Sigma0 },
      { RadiometricUnit::DigitalNumber, RadiometricUnit::Gamma0 },
      { RadiometricUnit::Beta0, RadiometricUnit::Sigma0 },
      { RadiometricUnit::Beta0, RadiometricUnit::Gamma0 },
      { RadiometricUnit::Sigma0, RadiometricUnit::Gamma0 },
      // Derived products (SarPairMetric / SarTexture / SpectralIndices) admit
      // no transitions at all: identity only, fail closed.
    };

    /// The layer source when it is a plain local file path; empty for
    /// provider URIs. #1467: layer->source() is a provider URI, not
    /// necessarily a path — Qt resources (":/…"), GDAL subdataset
    /// descriptors ("GTIFF_DIR:…", "HDF5:…"), and remote schemes
    /// ("https://…", "WMS:…") must skip the GDALOpenEx metadata round-trip
    /// instead of being opened. Classification — including POSIX filenames
    /// that legally contain ':' beyond a drive letter, #1469 review — lives
    /// in local_source::isPlainLocalPath: one authority, shared with the
    /// header-only preflight consumers.
    QString layerLocalFilePath( const QgsRasterLayer *layer )
    {
      if ( !layer )
        return QString();
      const QString source = layer->source();
      return local_source::isPlainLocalPath( source ) ? source : QString();
    }
  } // namespace

  QString RadiometricState::metadataKey()
  {
    return QStringLiteral( "SICNU_RADIOMETRIC_STATE" );
  }

  bool RadiometricState::canTransition( RadiometricUnit from, RadiometricUnit to ) noexcept
  {
    if ( from == to )
      return true;
    for ( const Transition &t : kLawfulTransitions )
    {
      if ( t.from == from && t.to == to )
        return true;
    }
    return false;
  }

  QString RadiometricState::unitToString( RadiometricUnit unit )
  {
    // #1450: the canonical production spelling (one vocabulary with the
    // operators) instead of an FSM-private uppercase dialect.
    const QString token = vocabulary::canonicalToken( unit );
    if ( !token.isEmpty() )
      return token;
    return QStringLiteral( "DIGITAL_NUMBER" );
  }

  RadiometricUnit RadiometricState::stringToUnit( const QString &str )
  {
    // An absent marker (empty after trim) is missing metadata, not a wrong
    // one: the rawest interpretation stays lawful (ADR 0158).
    RadiometricUnit unit = RadiometricUnit::DigitalNumber;
    if ( !vocabulary::lookup( str, &unit ) )
    {
      // Any unrecognized PRESENT marker degrades to the rawest
      // interpretation (ADR 0158 fail-safe default). Callers that must
      // distinguish unknown from DN use vocabulary::isKnownToken().
      return RadiometricUnit::DigitalNumber;
    }
    return unit;
  }

  RadiometricUnit RadiometricState::layerUnit( const QgsRasterLayer *layer )
  {
    if ( !layer )
      return RadiometricUnit::DigitalNumber;

    const QVariant prop = layer->customProperty( metadataKey() );
    if ( prop.isValid() && !prop.toString().isEmpty() )
      return stringToUnit( prop.toString() );

    // File-level fallback: GDAL DEFAULT-domain metadata item, so a state
    // written by a previous session (or another tool) is still honored.
    // Provider-URI sources are skipped (see layerLocalFilePath, #1467).
    const QString path = layerLocalFilePath( layer );
    if ( !path.isEmpty() )
    {
      if ( GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                                         nullptr, nullptr, nullptr ) )
      {
        const char *value = GDALGetMetadataItem( ds, metadataKey().toUtf8().constData(), nullptr );
        const RadiometricUnit unit = value ? stringToUnit( QString::fromUtf8( value ) )
                                           : RadiometricUnit::DigitalNumber;
        GDALClose( ds );
        return unit;
      }
    }
    return RadiometricUnit::DigitalNumber;
  }

  void RadiometricState::validateBandPreflight( const QgsRasterLayer *layer, RadiometricUnit required )
  {
    const RadiometricUnit actual = layerUnit( layer );
    if ( canTransition( actual, required ) )
      return;

    const QString message = QStringLiteral(
      "Radiometric state mismatch: layer is %1, operator requires %2 — transition %1→%2 is "
      "physically unlawful (ADR 0158). Run the upstream calibration/correction operator first." )
      .arg( unitToString( actual ), unitToString( required ) );
    throw RadiometricStateMismatchException( message );
  }

  bool RadiometricState::setLayerRadiometricState( QgsRasterLayer *layer, RadiometricUnit unit )
  {
    if ( !layer )
      return false;

    const QString value = unitToString( unit );
    layer->setCustomProperty( metadataKey(), value );

    // Best-effort persistence next to the data. The in-session custom
    // property is authoritative; a read-only dataset simply skips this.
    const QString path = layerLocalFilePath( layer );
    if ( !path.isEmpty() )
    {
      if ( GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_UPDATE,
                                         nullptr, nullptr, nullptr ) )
      {
        GDALSetMetadataItem( ds, metadataKey().toUtf8().constData(), value.toUtf8().constData(), nullptr );
        GDALClose( ds );
      }
    }
    return true;
  }
} // namespace exp_radiometric
