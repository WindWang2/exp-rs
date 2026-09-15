// src/analysis/atmospheric/fast_6s_lookup.cpp — fast 6S-style LUT (D13)
#include "atmospheric/fast_6s_lookup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace exp_radiometric
{
  namespace
  {
    constexpr double kPi = 3.14159265358979323846;

    // ── Grid axis definitions (uniform except where noted) ──────────────────
    constexpr int kAodSteps = 6;
    constexpr double kAodAxis[kAodSteps] = { 0.01, 0.1, 0.3, 0.6, 1.0, 2.0 };
    constexpr int kWvSteps = 6;
    constexpr double kWvAxis[kWvSteps] = { 0.2, 0.8, 1.5, 2.5, 4.0, 5.0 };
    constexpr int kSolarSteps = 6;
    constexpr double kSolarAxis[kSolarSteps] = { 0.0, 15.0, 30.0, 45.0, 60.0, 75.0 };
    constexpr int kViewSteps = 5;
    constexpr double kViewAxis[kViewSteps] = { 0.0, 15.0, 30.0, 45.0, 60.0 };
    constexpr int kAzimuthSteps = 5;
    constexpr double kAzimuthAxis[kAzimuthSteps] = { 0.0, 45.0, 90.0, 135.0, 180.0 };

    constexpr int kDimCount = 6; // wavelength, aod, wv, solar, view, azimuth

    // ── Compact physics parameterization (documented in DECISIONS.md D-02) ──

    /// Rayleigh optical depth of a standard atmosphere at λ (nm) — the
    /// classic λ^-4 power law with the Leckner correction terms, sea level.
    double rayleighOpticalDepth( double wavelengthNm )
    {
      const double um = wavelengthNm / 1000.0;
      // Leckner sea-level correction: (1 + 0.0113·λ⁻² + 0.00013·λ⁻⁴),
      // λ in µm — the correction grows toward the UV.
      const double um2 = um * um;
      const double um4 = um2 * um2;
      return 0.008569 * std::pow( um, -4.0 ) *
             ( 1.0 + 0.0113 / um2 + 0.00013 / um4 );
    }

    /// Ångström aerosol optical depth: tau(λ) = aod550·(550/λ)^1.3.
    double aerosolOpticalDepth( double wavelengthNm, double aod550 )
    {
      return aod550 * std::pow( 550.0 / wavelengthNm, 1.3 );
    }

    /// Gaussian-band water-vapor absorption coefficient per (g/cm²): the
    /// known near-IR absorption windows (weak 720/820, strong 940/1130,
    /// opaque 1400/1870 nm).
    double waterVaporAbsorption( double wavelengthNm )
    {
      struct Window { double center; double width; double amp; };
      static constexpr Window kWindows[] = {
        { 720.0, 20.0, 0.010 }, { 820.0, 30.0, 0.020 }, { 940.0, 50.0, 0.100 },
        { 1130.0, 40.0, 0.080 }, { 1400.0, 60.0, 0.350 }, { 1870.0, 80.0, 0.300 },
      };
      double k = 0.0;
      for ( const Window &w : kWindows )
      {
        const double d = wavelengthNm - w.center;
        k += w.amp * std::exp( -( d * d ) / ( 2.0 * w.width * w.width ) );
      }
      return k;
    }

    /// Rayleigh scattering phase function with molecular depolarization
    /// Δ = 0.0272, normalized over the scattering angle Θ.
    double rayleighPhase( double cosTheta )
    {
      constexpr double kDepolarization = 0.0272;
      const double num = 0.75 * ( 1.0 + kDepolarization + ( 1.0 - kDepolarization ) * cosTheta * cosTheta );
      return num / ( 1.0 + 2.0 * kDepolarization );
    }

    /// Henyey-Greenstein aerosol phase, asymmetry g = 0.7.
    double aerosolPhase( double cosTheta )
    {
      constexpr double kG = 0.7;
      const double g2 = kG * kG;
      return ( 1.0 - g2 ) / std::pow( 1.0 + g2 - 2.0 * kG * cosTheta, 1.5 );
    }

    /// Single grid cell evaluation. Angles arrive in degrees.
    Lut6sEntry evaluateCell( double wavelengthNm, double aod550, double waterVaporGcm2,
                             double solarZenithDeg, double viewZenithDeg, double relAzimuthDeg )
    {
      const double muS = std::max( std::cos( solarZenithDeg * kPi / 180.0 ), 1e-3 );
      const double muV = std::max( std::cos( viewZenithDeg * kPi / 180.0 ), 1e-3 );
      const double sinS = std::sin( solarZenithDeg * kPi / 180.0 );
      const double sinV = std::sin( viewZenithDeg * kPi / 180.0 );
      const double cosAzimuth = std::cos( relAzimuthDeg * kPi / 180.0 );
      // Phase-angle cosine of the geometry (source/observer same side = +1);
      // the photon-direction scattering angle Θ satisfies cosΘ = -cosScatter
      // (overhead sun + nadir view is 180° backscatter, cosΘ = -1).
      const double cosScatter = std::clamp( -( muS * muV + sinS * sinV * cosAzimuth ), -1.0, 1.0 );

      const double tauR = rayleighOpticalDepth( wavelengthNm );
      const double tauA = aerosolOpticalDepth( wavelengthNm, aod550 );
      const double tauTotal = tauR + tauA;

      // Path reflectance: single-scattering thin-limit form with the exact
      // slant airmass product — ω₀·τ·P(Θ)/(4·μs·μv) — with separate Rayleigh
      // (depolarized phase) and aerosol (HG phase, ω₀ = 0.95) contributions.
      const double scatteringGeometry = 1.0 / ( 4.0 * muS * muV );
      double rhoA = tauR * rayleighPhase( cosScatter ) * scatteringGeometry;
      rhoA += 0.95 * tauA * aerosolPhase( cosScatter ) * scatteringGeometry;

      // Total transmittances: extinction along each slant path, with
      // water-vapor absorption on the solar and view legs.
      const double kWater = waterVaporAbsorption( wavelengthNm );
      const double down = std::exp( -tauTotal / muS - kWater * waterVaporGcm2 / muS );
      const double up = std::exp( -tauTotal / muV - 0.5 * kWater * waterVaporGcm2 / muV );

      // Spherical albedo of the atmosphere: bounded, monotone in τ — the
      // classic two-stream closure S = (1 - T̄)/(1 + T̄) on the mean flux.
      const double meanTransmittance = 0.5 * ( down + up );
      const double sphericalAlbedo = std::clamp( ( 1.0 - meanTransmittance ) / ( 1.0 + meanTransmittance ),
                                                 0.0, 0.5 );

      return Lut6sEntry{
        static_cast<float>( rhoA ),
        static_cast<float>( down ),
        static_cast<float>( up ),
        static_cast<float>( sphericalAlbedo ) };
    }

    /// Flat grid storage: [wl][aod][wv][solar][view][azimuth], last axis fastest.
    constexpr int kWlCount = Fast6sLookup::kWavelengthSteps;
    constexpr size_t kGridSize = static_cast<size_t>( kWlCount ) * kAodSteps * kWvSteps *
                                 kSolarSteps * kViewSteps * kAzimuthSteps;

    struct GridHolder
    {
      std::array<Lut6sEntry, kGridSize> cells;
      explicit GridHolder( std::array<Lut6sEntry, kGridSize> &&c ) : cells( std::move( c ) ) {}
    };

    size_t cellIndex( int wl, int aod, int wv, int solar, int view, int azimuth )
    {
      return ( ( ( ( static_cast<size_t>( wl ) * kAodSteps + aod ) * kWvSteps + wv ) * kSolarSteps + solar ) *
               kViewSteps + view ) *
             kAzimuthSteps + azimuth;
    }

    const GridHolder &grid()
    {
      static const GridHolder holder = [] {
        std::array<Lut6sEntry, kGridSize> cells;
        for ( int wl = 0; wl < kWlCount; ++wl )
          for ( int aod = 0; aod < kAodSteps; ++aod )
            for ( int wv = 0; wv < kWvSteps; ++wv )
              for ( int solar = 0; solar < kSolarSteps; ++solar )
                for ( int view = 0; view < kViewSteps; ++view )
                  for ( int azimuth = 0; azimuth < kAzimuthSteps; ++azimuth )
                    cells[cellIndex( wl, aod, wv, solar, view, azimuth )] =
                      evaluateCell( Fast6sLookup::kWavelengthMinNm + wl * Fast6sLookup::kWavelengthStepNm,
                                    kAodAxis[aod], kWvAxis[wv], kSolarAxis[solar], kViewAxis[view],
                                    kAzimuthAxis[azimuth] );
        return GridHolder( std::move( cells ) );
      }();
      return holder;
    }

    /// Axis location: nearest-low index + fraction in [0, 1] for a uniform
    /// grid described by (axis, steps). Values outside are clamped to 0/last.
    struct AxisLocation
    {
      int lo = 0;
      double frac = 0.0;
    };

    AxisLocation locateUniform( double value, double min, double step, int steps )
    {
      AxisLocation loc;
      if ( !std::isfinite( value ) )
        return loc; // NaN/Inf clamps to the first grid node (never UB casts)
      const double raw = ( value - min ) / step;
      if ( raw <= 0.0 )
        return loc;
      if ( raw >= steps - 1 )
      {
        loc.lo = steps - 2;
        loc.frac = 1.0;
        return loc;
      }
      loc.lo = static_cast<int>( raw );
      loc.frac = raw - loc.lo;
      return loc;
    }

    AxisLocation locateTable( double value, const double *axis, int steps )
    {
      if ( value <= axis[0] )
        return {};
      if ( value >= axis[steps - 1] )
        return { steps - 2, 1.0 };
      int lo = 0;
      while ( lo + 1 < steps - 1 && axis[lo + 1] <= value )
        ++lo;
      return { lo, ( value - axis[lo] ) / ( axis[lo + 1] - axis[lo] ) };
    }
  } // namespace

  Lut6sEntry Fast6sLookup::interpolateCoefficients( double centralWavelengthNm,
                                                    const Atmosphere6sParams &params )
  {
    const AxisLocation wl = locateUniform( centralWavelengthNm, kWavelengthMinNm, kWavelengthStepNm, kWlCount );
    const AxisLocation aod = locateTable( params.aod550, kAodAxis, kAodSteps );
    const AxisLocation wv = locateTable( params.waterVaporGcm2, kWvAxis, kWvSteps );
    const AxisLocation solar = locateTable( params.solarZenithDeg, kSolarAxis, kSolarSteps );
    const AxisLocation view = locateTable( params.viewZenithDeg, kViewAxis, kViewSteps );
    const AxisLocation azimuth = locateTable( params.relAzimuthDeg, kAzimuthAxis, kAzimuthSteps );

    // 6-D multi-linear interpolation over the 64 surrounding cells.
    const AxisLocation *locs[kDimCount] = { &wl, &aod, &wv, &solar, &view, &azimuth };
    double weights[kDimCount][2];
    int corner[kDimCount];
    for ( int d = 0; d < kDimCount; ++d )
    {
      corner[d] = 0;
      weights[d][0] = 1.0 - locs[d]->frac;
      weights[d][1] = locs[d]->frac;
    }

    double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
    for ( ;; )
    {
      const double w = weights[0][corner[0]] * weights[1][corner[1]] * weights[2][corner[2]] *
                       weights[3][corner[3]] * weights[4][corner[4]] * weights[5][corner[5]];
      const Lut6sEntry &cell = grid().cells[cellIndex( wl.lo + corner[0], aod.lo + corner[1],
                                                       wv.lo + corner[2], solar.lo + corner[3],
                                                       view.lo + corner[4], azimuth.lo + corner[5] )];
      acc[0] += w * cell.atmosphericReflectance;
      acc[1] += w * cell.downwardTransmittance;
      acc[2] += w * cell.upwardTransmittance;
      acc[3] += w * cell.sphericalAlbedo;

      int d = kDimCount - 1;
      for ( ; d >= 0; --d )
      {
        corner[d] ^= 1;
        if ( corner[d] == 1 )
          break;
        corner[d] = 0;
      }
      if ( d < 0 )
        break;
    }

    return Lut6sEntry{ static_cast<float>( acc[0] ), static_cast<float>( acc[1] ),
                       static_cast<float>( acc[2] ), static_cast<float>( acc[3] ) };
  }

  bool Fast6sLookup::invertBoaReflectance( const float *toa, float *boa, size_t count,
                                           const Lut6sEntry &lut, float noData )
  {
    if ( toa == nullptr || boa == nullptr || count == 0 )
      return false;
    if ( !( lut.downwardTransmittance > 0.0f ) || !( lut.upwardTransmittance > 0.0f ) )
      return false;

    const float rhoA = lut.atmosphericReflectance;
    const float tSTv = lut.downwardTransmittance * lut.upwardTransmittance;
    const float s = lut.sphericalAlbedo;

    for ( size_t i = 0; i < count; ++i )
    {
      const float value = toa[i];
      if ( value == noData )
      {
        boa[i] = noData;
        continue;
      }
      if ( !std::isfinite( value ) )
      {
        boa[i] = std::numeric_limits<float>::quiet_NaN();
        continue;
      }
      const float x = value - rhoA;
      const float denom = tSTv + s * x;
      if ( !( denom > 0.0f ) || !std::isfinite( denom ) )
      {
        // Atmosphere so bright the closed form has no physical root.
        boa[i] = std::numeric_limits<float>::quiet_NaN();
        continue;
      }
      boa[i] = std::clamp( x / denom, 0.0f, 1.0f );
    }
    return true;
  }

  bool Fast6sLookup::executeDos2( const float *radiance, float *boa, size_t count,
                                  float darkObjectRadiance, double sunElevationDeg,
                                  double esun, double earthSunDistAu, float noData )
  {
    if ( radiance == nullptr || boa == nullptr || count == 0 )
      return false;
    if ( !( esun > 0.0 ) )
      return false;

    const double sinElevation = std::sin( sunElevationDeg * kPi / 180.0 );
    if ( sinElevation <= 0.0 )
      return false; // sun below the horizon: no physically meaningful DOS2

    // DOS2 (Chavez 1996): T_z = sin(elev), T_v = 1.0
    const double tV = 1.0;
    const double d2 = earthSunDistAu * earthSunDistAu;
    const double denominator = esun * sinElevation * tV * sinElevation;

    for ( size_t i = 0; i < count; ++i )
    {
      const float value = radiance[i];
      if ( value == noData )
      {
        boa[i] = noData;
        continue;
      }
      if ( !std::isfinite( value ) )
      {
        boa[i] = std::numeric_limits<float>::quiet_NaN();
        continue;
      }
      // Dark-object pixels sit at the path-radiance floor: reflectance 0.
      const double excess = static_cast<double>( value ) - darkObjectRadiance;
      boa[i] = excess <= 0.0
                 ? 0.0f
                 : static_cast<float>( std::clamp( kPi * excess * d2 / denominator, 0.0, 1.0 ) );
    }
    return true;
  }
} // namespace exp_radiometric
