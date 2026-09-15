// src/processing/algorithms/atmospheric_provider.h — the optional
// surface-reflectance provider seam (radiometric-physics-11, work package C).
//
// AtmosphericCorrection owns the built-in image-space methods (DOS1, DOS2,
// QUAC) as a closed Method enum. Physical LUT / 6S-class correction needs a
// different shape: a named provider that declares which auxiliary inputs it
// requires and may be absent from a given installation. This header is that
// seam — an in-process registry plus an abstract provider interface. It
// adds no dependency and ships no radiative-transfer implementation; a
// future 6S-class LUT registers itself here and DOS/QUAC stay available as
// built-in providers under their own ids.
//
// Fail-closed contract (GOAL Oracle 2): a caller asking for a provider that
// is not registered gets a typed refusal naming the requested id and the
// registered fallbacks — NEVER a silent degrade to DOS. A registered
// provider whose required auxiliary inputs are missing/degenerate is
// refused with the missing items named. Callers that want image-space
// behaviour explicitly ask for "dos1"/"dos2"/"quac".
//
// Ownership & lifetime: the registry is NON-OWNING — the caller keeps the
// provider alive for the process lifetime (typically a function-local static
// or a leaked start-up allocation). Registration is idempotent and happens
// at start-up under a mutex; returned pointers stay valid because providers
// are never unregistered.
#pragma once

#include <QString>
#include <QStringList>

#include <cstddef>

namespace AtmosphericProvider
{

/// Auxiliary inputs a provider may require. Fields left at their sentinel
/// values are "unknown" and are refused by providers that need them.
struct CorrectionInputs
{
    /// Scene geometry in degrees (platform conventions: elevation (0, 90],
    /// azimuth [0, 360) from north). hasSunGeometry must be true for
    /// providers that declare needsSunGeometry.
    double sunElevationDeg = 0.0;
    double sunAzimuthDeg = 0.0;
    bool hasSunGeometry = false;

    /// Scene-level dark-object TOA reflectance (e.g.
    /// AtmosphericCorrection::findDarkObjectByHistogram output). Required by
    /// DOS-family providers; must be finite and >= 0.
    float toaDarkLevel = -1.0f;

    /// Two-way transmittance for DOS2, must lie in (0, 1].
    float transmittance = -1.0f;

    /// 6S-class auxiliary parameters (sentinel < 0 = unknown).
    double aod550 = -1.0;          ///< aerosol optical depth @ 550 nm
    double waterVapourGcm2 = -1.0; ///< precipitable water vapour [g/cm²]
    double targetElevationM = -1.0; ///< target elevation [m] (sea level = 0)
};

/// What a provider needs beyond the pixel buffer. Each `needs*` flag turns
/// the matching CorrectionInputs field (or group) into a hard requirement.
struct Requirements
{
    bool needsSunGeometry = false;  ///< sunElevation/Azimuth + hasSunGeometry
    bool needsDarkLevel = false;    ///< toaDarkLevel finite, >= 0
    bool needsTransmittance = false; ///< transmittance in (0, 1]
    bool needsAod = false;          ///< aod550 >= 0
    bool needsWaterVapour = false;  ///< waterVapourGcm2 >= 0
    bool needsTargetElevation = false; ///< targetElevationM >= 0
};

class SurfaceReflectanceProvider
{
  public:
    virtual ~SurfaceReflectanceProvider();

    /// Registry id (stable, lowercase, e.g. "dos1", "dos2", "quac").
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Requirements requirements() const = 0;

    /// One-band TOA reflectance → surface reflectance. NaN pixels propagate
    /// as NaN; negative results pass through FINITE (the house Chavez kernel
    /// does not clip — RadiometricQa::FlagNegative reports them downstream).
    /// Implementations must refuse (return false + typed message) when
    /// requirements are unmet — the registry has already validated them, so
    /// a false return from a live provider means a degenerate value.
    virtual bool toSurfaceReflectance( const float *toa, float *surface, size_t count,
                                       const CorrectionInputs &aux,
                                       QString *errorMessage = nullptr ) const;

    /// Multi-band form for image-statistics methods (QUAC needs the whole
    /// stack). Default: unsupported (returns false, "does not support
    /// multi-band correction") — callers must handle that refusal.
    virtual bool toSurfaceReflectanceMultiBand( const float *const *toaBands,
                                                float *const *surfaceBands, int bandCount,
                                                size_t pixels, const CorrectionInputs &aux,
                                                QString *errorMessage = nullptr ) const;
};

// -------------------------------------------------------------------------
// Registry
// -------------------------------------------------------------------------

/// Registers @p provider WITHOUT taking ownership — the caller must keep it
/// alive for the process lifetime. Duplicate ids are refused (returns false,
/// leaves the existing registration untouched).
bool registerProvider( SurfaceReflectanceProvider *provider );

/// Idempotent installation of the built-ins ("dos1", "dos2", "quac"
/// wrapping AtmosphericCorrection kernels). Safe to call repeatedly.
void ensureBuiltinProviders();

/// Registered ids in deterministic (sorted) order.
QStringList registeredIds();

/// Registry lookup; nullptr when @p id is unknown. Pointer stays valid for
/// the process lifetime.
const SurfaceReflectanceProvider *provider( const QString &id );

/// Typed resolution used by operators: looks up @p id and validates @p aux
/// against the provider's requirements. Returns false with @p errorMessage
/// naming the requested id, the registered alternatives (unknown id), or
/// each missing auxiliary input — the "no provider" refusal is never a
/// silent fallback.
bool resolve( const QString &id, const CorrectionInputs &aux,
              const SurfaceReflectanceProvider **out, QString *errorMessage = nullptr );

/// Validates @p aux against @p requirements (exposed for tests / preflight
/// reuse): returns false with a message naming every missing item.
bool checkRequirements( const Requirements &requirements, const CorrectionInputs &aux,
                        QString *errorMessage = nullptr );

} // namespace AtmosphericProvider
