// src/processing/algorithms/atmospheric_provider.cpp — see
// atmospheric_provider.h for the seam contract and fail-closed semantics.
#include "atmospheric_provider.h"

#include "atmospheric_correction.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace AtmosphericProvider
{

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

bool finite( double v )
{
    return std::isfinite( v );
}

// -------------------------------------------------------------------------
// Built-in providers wrapping the master AtmosphericCorrection kernels.
// -------------------------------------------------------------------------

/// Chavez DOS1 in TOA-reflectance space:
/// rho_surf = (rho_toa − rho_dark + 0.01) / 1
class Dos1Provider final : public SurfaceReflectanceProvider
{
  public:
    QString id() const override { return QStringLiteral( "dos1" ); }
    QString displayName() const override { return QStringLiteral( "DOS1 (Chavez dark-object subtraction)" ); }
    Requirements requirements() const override
    {
        Requirements r;
        r.needsDarkLevel = true;
        return r;
    }
    bool toSurfaceReflectance( const float *toa, float *surface, size_t count,
                               const CorrectionInputs &aux,
                               QString *errorMessage = nullptr ) const override
    {
        if ( !toa || !surface || count == 0 )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "dos1: null or empty buffers" );
            return false;
        }
        for ( size_t i = 0; i < count; ++i )
            surface[i] = AtmosphericCorrection::dosReflectance( toa[i], aux.toaDarkLevel, 1.0f );
        return true;
    }
};

/// Chavez DOS2 in TOA-reflectance space:
/// rho_surf = (rho_toa − rho_dark + 0.01) / T
class Dos2Provider final : public SurfaceReflectanceProvider
{
  public:
    QString id() const override { return QStringLiteral( "dos2" ); }
    QString displayName() const override { return QStringLiteral( "DOS2 (dark-object + transmittance)" ); }
    Requirements requirements() const override
    {
        Requirements r;
        r.needsDarkLevel = true;
        r.needsTransmittance = true;
        return r;
    }
    bool toSurfaceReflectance( const float *toa, float *surface, size_t count,
                               const CorrectionInputs &aux,
                               QString *errorMessage = nullptr ) const override
    {
        if ( !toa || !surface || count == 0 )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "dos2: null or empty buffers" );
            return false;
        }
        for ( size_t i = 0; i < count; ++i )
            surface[i] =
                AtmosphericCorrection::dosReflectance( toa[i], aux.toaDarkLevel, aux.transmittance );
        return true;
    }
};

/// QUAC adapter: image-statistics method, multi-band by definition.
class QuacProvider final : public SurfaceReflectanceProvider
{
  public:
    QString id() const override { return QStringLiteral( "quac" ); }
    QString displayName() const override { return QStringLiteral( "QUAC (Quick Atmospheric Correction)" ); }
    Requirements requirements() const override { return {}; }
    bool toSurfaceReflectance( const float *, float *, size_t,
                               const CorrectionInputs &,
                               QString *errorMessage = nullptr ) const override
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "quac: requires the multi-band correction form "
                                            "(needs the whole stack for its statistics)" );
        return false;
    }
    bool toSurfaceReflectanceMultiBand( const float *const *toaBands,
                                        float *const *surfaceBands, int bandCount,
                                        size_t pixels, const CorrectionInputs &,
                                        QString *errorMessage = nullptr ) const override
    {
        // Runs the house percentile-based kernel over the provided stack.
        // For raw-DN stacks callers can also use the house
        // AtmosphericCorrection::processFileMultiBand path directly; on this
        // seam the stack is expected in reflectance units (quantification-
        // scaled DN divided by its SICNU_NUMERIC_SCALE first).
        return AtmosphericCorrection::quac( toaBands, surfaceBands, bandCount, pixels,
                                            errorMessage );
    }
};

std::mutex &registryMutex()
{
    static std::mutex m;
    return m;
}

std::map<QString, std::unique_ptr<SurfaceReflectanceProvider>> &registryMap()
{
    static std::map<QString, std::unique_ptr<SurfaceReflectanceProvider>> m;
    return m;
}
} // namespace

SurfaceReflectanceProvider::~SurfaceReflectanceProvider() = default;

bool SurfaceReflectanceProvider::toSurfaceReflectance( const float *, float *, size_t,
                                                       const CorrectionInputs &,
                                                       QString *errorMessage ) const
{
    if ( errorMessage )
        *errorMessage = QStringLiteral( "%1: does not support single-band correction" ).arg( id() );
    return false;
}

bool SurfaceReflectanceProvider::toSurfaceReflectanceMultiBand( const float *const *,
                                                               float *const *, int, size_t,
                                                               const CorrectionInputs &,
                                                               QString *errorMessage ) const
{
    if ( errorMessage )
        *errorMessage = QStringLiteral( "%1: does not support multi-band correction" ).arg( id() );
    return false;
}

bool registerProvider( SurfaceReflectanceProvider *providerPtr )
{
    if ( !providerPtr || providerPtr->id().isEmpty() )
        return false;
    std::lock_guard<std::mutex> lock( registryMutex() );
    return registryMap()
        .emplace( providerPtr->id(), std::unique_ptr<SurfaceReflectanceProvider>( providerPtr ) )
        .second;
}

void ensureBuiltinProviders()
{
    static const bool installed = [] {
        const bool ok = registerProvider( new Dos1Provider() )
                        && registerProvider( new Dos2Provider() )
                        && registerProvider( new QuacProvider() );
        Q_ASSERT( ok );
        return ok;
    }();
    Q_UNUSED( installed );
}

QStringList registeredIds()
{
    ensureBuiltinProviders(); // never called with the registry lock held
    std::lock_guard<std::mutex> lock( registryMutex() );
    QStringList ids;
    for ( const auto &entry : registryMap() )
        ids.append( entry.first );
    std::sort( ids.begin(), ids.end() );
    return ids;
}

const SurfaceReflectanceProvider *provider( const QString &id )
{
    ensureBuiltinProviders(); // never called with the registry lock held
    std::lock_guard<std::mutex> lock( registryMutex() );
    const auto it = registryMap().find( id );
    return it == registryMap().end() ? nullptr : it->second.get();
}

bool checkRequirements( const Requirements &requirements, const CorrectionInputs &aux,
                        QString *errorMessage )
{
    QStringList missing;
    if ( requirements.needsSunGeometry && ( !aux.hasSunGeometry || !finite( aux.sunElevationDeg )
                                           || aux.sunElevationDeg <= 0.0
                                           || aux.sunElevationDeg > 90.0 || !finite( aux.sunAzimuthDeg ) ) )
        missing.append( QStringLiteral( "sun geometry (elevation/azimuth)" ) );
    if ( requirements.needsDarkLevel
         && ( !std::isfinite( aux.toaDarkLevel ) || aux.toaDarkLevel < 0.0f ) )
        missing.append( QStringLiteral( "scene dark-object TOA level" ) );
    if ( requirements.needsTransmittance
         && ( !std::isfinite( aux.transmittance ) || aux.transmittance <= 0.0f
              || aux.transmittance > 1.0f ) )
        missing.append( QStringLiteral( "atmospheric transmittance in (0, 1]" ) );
    if ( requirements.needsAod && ( !finite( aux.aod550 ) || aux.aod550 < 0.0 ) )
        missing.append( QStringLiteral( "aerosol optical depth @550nm" ) );
    if ( requirements.needsWaterVapour
         && ( !finite( aux.waterVapourGcm2 ) || aux.waterVapourGcm2 < 0.0 ) )
        missing.append( QStringLiteral( "water vapour column" ) );
    if ( requirements.needsTargetElevation
         && ( !finite( aux.targetElevationM ) || aux.targetElevationM < 0.0 ) )
        missing.append( QStringLiteral( "target elevation" ) );

    if ( !missing.isEmpty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "missing atmospheric correction inputs: %1" )
                                .arg( missing.join( QStringLiteral( "; " ) ) );
        return false;
    }
    return true;
}

bool resolve( const QString &id, const CorrectionInputs &aux,
              const SurfaceReflectanceProvider **out, QString *errorMessage )
{
    if ( out )
        *out = nullptr;
    const SurfaceReflectanceProvider *p = provider( id );
    if ( !p )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "atmospheric provider '%1' is not registered; "
                                            "available providers: %2" )
                                .arg( id, registeredIds().join( QStringLiteral( ", " ) ) );
        return false;
    }
    if ( !checkRequirements( p->requirements(), aux, errorMessage ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "%1: %2" ).arg( id, *errorMessage );
        return false;
    }
    if ( out )
        *out = p;
    return true;
}

} // namespace AtmosphericProvider
