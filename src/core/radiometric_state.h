// src/core/radiometric_state.h — radiometric unit FSM contract (D13 / ADR 0158)
#pragma once

#include <QString>
#include <stdexcept>

class QgsRasterLayer;

namespace exp_radiometric
{
    /// Physical radiometric units of a raster band. The optical states are
    /// ordered along the processing chain (raw sensor counts → calibrated
    /// physics); the SAR backscatter states (#1450) and derived-product
    /// states extend the vocabulary to everything operators actually write
    /// into SICNU_RADIOMETRIC_STATE.
    enum class RadiometricUnit
    {
        DigitalNumber = 0,    ///< Raw sensor counts (DN); SAR writers spell it "dn"
        Radiance,             ///< Spectral radiance L_lambda (W·m^-2·sr^-1·µm^-1)
        ToaReflectance,       ///< Top-Of-Atmosphere planetary reflectance rho_toa in [0, 1]
        BoaReflectance,       ///< Bottom-Of-Atmosphere surface reflectance rho_boa in [0, 1]; production spelling "surface_reflectance"
        BrightnessTemperature,///< Thermal brightness temperature T in Kelvin (K)
        // Values 5..7 are left reserved: the in-flight #1469 review branch
        // carries a file-local unrecognized-unit sentinel at 5; keeping the
        // gap lets both waves rebase without enumerator value collisions.
        Sigma0 = 8,           ///< SAR backscatter sigma-nought (radiometrically calibrated)
        Gamma0,               ///< SAR backscatter gamma-nought (terrain-flattened)
        Beta0,                ///< SAR backscatter beta-nought (ellipsoidal)
        SarPairMetric,        ///< Derived dimensionless SAR pair ratio ("sar_pair_metric") — never re-calibrates
        SarTexture,           ///< Derived GLCM texture product ("sar_texture") — never re-calibrates
        SpectralIndices       ///< Derived spectral index stack ("indices") — never re-calibrates
    };

    /// Single radiometric token vocabulary (#1450): every spelling operators
    /// write into SICNU_RADIOMETRIC_STATE, mapped once. Header-only and
    /// QtCore-only so the production preflight (eo_preflight) consumes the
    /// same table as the FSM without a qgis_core link edge. All matching is
    /// trimmed + case-insensitive; nothing here treats an unknown token as
    /// DigitalNumber (that legacy degrade lives in stringToUnit's fallback
    /// only — use lookup()/isKnownToken() when "unknown" must stay unknown).
    namespace vocabulary
    {
        struct TokenRow
        {
            const char *token;   ///< production spelling (lowercase)
            RadiometricUnit unit;
        };

        inline constexpr TokenRow kTokens[] = {
            { "digital_number", RadiometricUnit::DigitalNumber },
            { "dn", RadiometricUnit::DigitalNumber }, // sicnu::sar writer spelling
            { "radiance", RadiometricUnit::Radiance },
            { "toa_reflectance", RadiometricUnit::ToaReflectance },
            { "surface_reflectance", RadiometricUnit::BoaReflectance }, // SatelliteProducts writer
            { "boa_reflectance", RadiometricUnit::BoaReflectance },     // ADR 0158 alias
            { "brightness_temperature", RadiometricUnit::BrightnessTemperature },
            { "sigma0", RadiometricUnit::Sigma0 },
            { "gamma0", RadiometricUnit::Gamma0 },
            { "beta0", RadiometricUnit::Beta0 },
            { "sar_pair_metric", RadiometricUnit::SarPairMetric },
            { "sar_texture", RadiometricUnit::SarTexture },
            { "indices", RadiometricUnit::SpectralIndices },
        };

        /// True when @a raw (trimmed, case-insensitive) names a known unit.
        inline bool lookup( const QString &raw, RadiometricUnit *out )
        {
            const QString trimmed = raw.trimmed();
            for ( const TokenRow &row : kTokens )
            {
                if ( trimmed.compare( QLatin1String( row.token ), Qt::CaseInsensitive ) == 0 )
                {
                    if ( out )
                        *out = row.unit;
                    return true;
                }
            }
            return false;
        }

        /// Canonical production spelling (first table row for the unit).
        inline QString canonicalToken( RadiometricUnit unit )
        {
            for ( const TokenRow &row : kTokens )
                if ( row.unit == unit )
                    return QString::fromLatin1( row.token );
            return QString();
        }

        /// True when @a raw names a known unit.
        inline bool isKnownToken( const QString &raw )
        {
            return lookup( raw, nullptr );
        }

        /// Semantic equivalence for preflight comparisons: exact strings, or
        /// two KNOWN tokens naming the same unit (spelling variants like
        /// "surface_reflectance" vs "BOA_REFLECTANCE"). Two different
        /// unknown tokens are never equivalent (fail closed), and an unknown
        /// token is never equivalent to a known one.
        inline bool tokensEquivalent( const QString &a, const QString &b )
        {
            if ( a.trimmed().compare( b.trimmed(), Qt::CaseInsensitive ) == 0 )
                return true;
            RadiometricUnit ua, ub;
            if ( !lookup( a, &ua ) || !lookup( b, &ub ) )
                return false;
            return ua == ub;
        }
    } // namespace vocabulary

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
