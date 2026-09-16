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

    QString layerSourcePath( const QgsRasterLayer *layer )
    {
      return layer ? layer->source() : QString();
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
    if ( normalized == QLatin1String( "RADIANCE" ) )
      return RadiometricUnit::Radiance;
    if ( normalized == QLatin1String( "TOA_REFLECTANCE" ) )
      return RadiometricUnit::ToaReflectance;
    if ( normalized == QLatin1String( "SURFACE_REFLECTANCE" ) )
      return RadiometricUnit::BoaReflectance;
    if ( normalized == QLatin1String( "BRIGHTNESS_TEMPERATURE" ) )
      return RadiometricUnit::BrightnessTemperature;
    // "DIGITAL_NUMBER" and any unrecognized marker degrade to the rawest
    // interpretation (ADR 0158 fail-safe default).
    return RadiometricUnit::DigitalNumber;
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
    const QString path = layerSourcePath( layer );
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
    const QString path = layerSourcePath( layer );
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
