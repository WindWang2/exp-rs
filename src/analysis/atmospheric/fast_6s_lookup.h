// src/analysis/atmospheric/fast_6s_lookup.h — fast 6S-style radiative transfer LUT (D13)
#pragma once

#include <cstddef>

namespace exp_radiometric
{
    /// Atmospheric state driving the radiative-transfer lookup.
    struct Atmosphere6sParams
    {
        double aod550 = 0.2;          ///< Aerosol Optical Depth at 550nm [0.01, 2.0]
        double waterVaporGcm2 = 1.5;  ///< Integrated water vapor column [0.2, 5.0] g/cm²
        double solarZenithDeg = 30.0; ///< Solar zenith angle in degrees [0, 75]
        double viewZenithDeg = 0.0;   ///< View zenith angle in degrees [0, 60]
        double relAzimuthDeg = 0.0;   ///< Relative azimuth angle in degrees [0, 180]
    };

    /// One cell of the 6S radiative-transfer coefficient grid (plain-6S
    /// parameterization, ADR-consistent): path reflectance, total
    /// downward/upward transmittances and atmospheric spherical albedo.
    struct Lut6sEntry
    {
        float atmosphericReflectance;  ///< rho_a
        float downwardTransmittance;   ///< T(theta_s)
        float upwardTransmittance;     ///< T(theta_v)
        float sphericalAlbedo;         ///< S
    };

    /// Fast 6S-style atmospheric correction.
    ///
    /// A deterministic coefficient grid (wavelength 350–2500 nm × AOD550 ×
    /// water vapor × solar/view zenith × relative azimuth) is built once from
    /// a compact physics parameterization — Rayleigh optical depth with
    /// depolarization phase, Ångström aerosols with Henyey-Greenstein phase,
    /// Gaussian-band water-vapor absorption — and queried by trilinear-grade
    /// multi-linear interpolation. All values are reproducible offline; no
    /// external 6S binary is consulted.
    class Fast6sLookup
    {
      public:
        /// Interpolates the 6S radiative transfer coefficients from the
        /// precomputed LUT grid. Wavelengths outside [350, 2500] nm and
        /// parameters outside their physical domains are clamped to the grid.
        static Lut6sEntry interpolateCoefficients( double centralWavelengthNm,
                                                   const Atmosphere6sParams &params );

        /// Closed-form inversion of the 6S forward model from TOA reflectance
        /// to BOA surface reflectance:
        ///   rho_boa = (rho* - rho_a) / (T_s·T_v + S·(rho* - rho_a))
        /// Results are clamped to [0, 1] (unphysical excursions are a QA
        /// concern for callers); denominator collapse yields NaN; NoData
        /// passes through.
        static bool invertBoaReflectance( const float *toa, float *boa, size_t count,
                                          const Lut6sEntry &lut, float noData = -9999.0f );

        /// Image-based DOS2 atmospheric correction baseline (Chavez 1996):
        ///   rho_boa = pi·(L - L_path)·d² / (ESUN·sin²(elev)·T_v),  T_v = 1
        /// Radiance at or below the dark-object level maps to 0; NoData and
        /// NaN pass through. Requires esun > 0 and sun above the horizon.
        static bool executeDos2( const float *radiance, float *boa, size_t count,
                                 float darkObjectRadiance, double sunElevationDeg,
                                 double esun, double earthSunDistAu, float noData = -9999.0f );

        /// Grid geometry (exposed for tests and callers aligning band sets):
        /// uniform wavelength axis, 350..2500 nm.
        static constexpr double kWavelengthMinNm = 350.0;
        static constexpr double kWavelengthStepNm = 50.0;
        static constexpr int kWavelengthSteps = 44; ///< nodes at 350 + {0..43}·50 = 350..2500 nm
    };

} // namespace exp_radiometric
