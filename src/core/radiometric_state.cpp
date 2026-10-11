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
    };

    // #1450: fail-closed state for a present-but-unrecognized unit marker.
    // Degrading it to DN would make DN→Radiance look lawful and allow double
    // calibration of a layer that may already be calibrated, so no transition
    // (identity excepted) is lawful out of this state. The public enum has no
    // Unknown member; a scoped enum without a fixed underlying type holds any
    // value of its enumerator range (0..7 here), so this file-local sentinel
    // needs no header change.
    constexpr RadiometricUnit kUnrecognizedUnit = static_cast<RadiometricUnit>( 5 );

    /// The layer source when it is a plain local file path; empty for
    /// provider URIs. #1467: layer->source() is a provider URI, not
    /// necessarily a path — Qt resources (":/…"), GDAL subdataset
    /// descriptors ("GTIFF_DIR:…", "HDF5:…"), and remote schemes
    /// ("https://…", "WMS:…") must skip the GDALOpenEx metadata round-trip
    /// instead of being opened. The only colon a local path may carry is a
    /// Windows drive letter ("C:/…" / "C:\…"); anything else is a URI.
    QString layerLocalFilePath( const QgsRasterLayer *layer )
    {
      if ( !layer )
        return QString();
      const QString source = layer->source();
      const int colon = source.indexOf( QLatin1Char( ':' ) );
      if ( colon < 0 )
        return source; // POSIX / UNC / relative path
      if ( colon != 1 || source.size() < 3 )
        return QString();
      const QChar afterColon = source.at( 2 );
      if ( !source.at( 0 ).isLetter() ||
           ( afterColon != QLatin1Char( '/' ) && afterColon != QLatin1Char( '\\' ) ) )
        return QString();
      return source;
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
    if ( unit == kUnrecognizedUnit )
      return QStringLiteral( "UNKNOWN" );
    switch ( unit )
    {
      case RadiometricUnit::DigitalNumber:
        return QStringLiteral( "DIGITAL_NUMBER" );
      case RadiometricUnit::Radiance:
        return QStringLiteral( "RADIANCE" );
      case RadiometricUnit::ToaReflectance:
        return QStringLiteral( "TOA_REFLECTANCE" );
      case RadiometricUnit::BoaReflectance:
        return QStringLiteral( "SURFACE_REFLECTANCE" );
      case RadiometricUnit::BrightnessTemperature:
        return QStringLiteral( "BRIGHTNESS_TEMPERATURE" );
    }
    return QStringLiteral( "DIGITAL_NUMBER" );
  }

  RadiometricUnit RadiometricState::stringToUnit( const QString &str )
  {
    const QString normalized = str.trimmed().toUpper();
    // An absent marker (empty after trim) is missing metadata, not a wrong
    // one: the rawest interpretation stays lawful (ADR 0158).
    if ( normalized.isEmpty() )
      return RadiometricUnit::DigitalNumber;
    if ( normalized == QLatin1String( "DIGITAL_NUMBER" ) )
      return RadiometricUnit::DigitalNumber;
    if ( normalized == QLatin1String( "RADIANCE" ) )
      return RadiometricUnit::Radiance;
    if ( normalized == QLatin1String( "TOA_REFLECTANCE" ) )
      return RadiometricUnit::ToaReflectance;
    if ( normalized == QLatin1String( "SURFACE_REFLECTANCE" ) )
      return RadiometricUnit::BoaReflectance;
    if ( normalized == QLatin1String( "BRIGHTNESS_TEMPERATURE" ) )
      return RadiometricUnit::BrightnessTemperature;
    if ( normalized == QLatin1String( "UNKNOWN" ) )
      return kUnrecognizedUnit;
    // #1450: fail closed — a present-but-unrecognized marker must not
    // degrade to DN, or DN→Radiance would look lawful and an already
    // calibrated layer could be calibrated a second time. The unrecognized
    // state admits no transition, so every preflight refuses until the
    // marker is fixed.
    return kUnrecognizedUnit;
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
