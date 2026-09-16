// src/core/radiometric_state.h — radiometric unit FSM contract (D13 / ADR 0158)
#pragma once

#include <QString>
#include <stdexcept>

class QgsRasterLayer;

namespace exp_radiometric
{
    /// Physical radiometric units of a raster band, ordered along the
    /// processing chain (raw sensor counts → calibrated physics).
    enum class RadiometricUnit
    {
        DigitalNumber = 0,    ///< Raw sensor counts (DN)
        Radiance,             ///< Spectral radiance L_lambda (W·m^-2·sr^-1·µm^-1)
        ToaReflectance,       ///< Top-Of-Atmosphere planetary reflectance rho_toa in [0, 1]
        BoaReflectance,       ///< Bottom-Of-Atmosphere surface reflectance rho_boa in [0, 1]
        BrightnessTemperature ///< Thermal brightness temperature T in Kelvin (K)
    };

    /// Thrown by validateBandPreflight when a layer's radiometric state does
    /// not lawfully transition to the required unit (ADR 0158 — fail closed).
    class RadiometricStateMismatchException : public std::runtime_error
    {
      public:
        explicit RadiometricStateMismatchException( const QString &message )
            : std::runtime_error( message.toStdString() ) {}
    };

    /// Directed-acyclic radiometric state machine (see
    /// docs/adr/0158-radiometric-physics-state-system.md). Identity is always
    /// lawful; backwards inversions (e.g. BOA→DN) and unit-jumping shortcuts
    /// (DN→BOA, DN→BT, TOA→BT) are unlawful.
    class RadiometricState
    {
      public:
        /// Metadata key carrying the unit marker (QgsMapLayer custom property,
        /// persisted as GDAL DEFAULT-domain metadata item).
        static QString metadataKey();

        /// Validates whether a direct transition from unit 'from' to 'to' is
        /// physically lawful.
        static bool canTransition( RadiometricUnit from, RadiometricUnit to ) noexcept;

        /// Converts RadiometricUnit to the canonical uppercase metadata string.
        static QString unitToString( RadiometricUnit unit );

        /// Parses a metadata string (case-insensitive) to RadiometricUnit.
        /// Unrecognized strings degrade to DigitalNumber (rawest fail-safe).
        static RadiometricUnit stringToUnit( const QString &str );

        /// Reads the layer's radiometric unit: custom property, then GDAL
        /// file metadata; missing marker ⇒ DigitalNumber.
        static RadiometricUnit layerUnit( const QgsRasterLayer *layer );

        /// Preflight check on a raster layer: reads SICNU_RADIOMETRIC_STATE
        /// metadata and throws RadiometricStateMismatchException when the
        /// layer's state cannot lawfully reach @p required (equal states and
        /// lawful forward transitions pass).
        static void validateBandPreflight( const QgsRasterLayer *layer, RadiometricUnit required );

        /// Writes the radiometric state marker into the layer (custom
        /// property, plus best-effort GDAL file metadata). Returns false only
        /// for a null layer.
        static bool setLayerRadiometricState( QgsRasterLayer *layer, RadiometricUnit unit );
    };

} // namespace exp_radiometric
