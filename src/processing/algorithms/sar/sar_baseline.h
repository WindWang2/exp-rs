// sar_baseline.h — pair-level InSAR input truth (Advanced InSAR 11.0,
// package A).
//
// The 10.0 InSAR chain validated scenes one operator at a time; nothing
// above the kernels owned a single versioned statement of "what two
// interferometric partners are". This module is that statement: a pair of
// scene truths (acquisition UTC, radar wavelength, orbit states, optional
// absolute anchor of the orbit time base) plus the pair-level derivations
// every later package consumes (temporal baseline, wavelength consistency,
// per-ground-point interferometric baseline, height ambiguity).
//
// Authority reuse (nothing here re-implements geometry):
//   * OrbitSegment / parseOrbitStates / interpolateState — sar_orbit.h
//   * zero-Doppler crossing (forwardRangeDoppler) — sar_orbit.h
//   * interferometricBaseline (B∥/B⊥/|Δr|) — sar_orbit.h
//   * Wgs84 geodetic↔ECEF — sar_orbit.h
// Time bases: InSarSceneTruth::acquisitionUtcSec is absolute UTC seconds
// (the SICNU_SAR_ACQUISITION_UTC contract, sar_temporal_events.h); the
// OrbitSegment times live on the SICNU_SAR_ORBIT_STATES base ("UTC seconds
// relative to the scene azimuth epoch"). azimuthStartUtcSec, when declared
// and finite, anchors the orbit base to absolute UTC so the two bases can
// be cross-checked; when absent the pair stays valid — the geometric
// kernels below never need absolute time (a zero-Doppler crossing is a
// geometric event) — and orbitsShareAbsoluteBase records the fact.
//
// Failure semantics: every refusal returns false with a domain-coded
// message (the message prefixes are the closed failure-mode vocabulary of
// capability_catalog.cpp): WAVELENGTH_INCOMPATIBLE, ORBIT_SEGMENT_INVALID,
// SCENE_TRUTH_INVALID, ORBIT_EPOCH_MISMATCH,
// BASELINE_NO_ZERO_DOPPLER_MASTER, BASELINE_NO_ZERO_DOPPLER_SLAVE,
// BASELINE_STATE_INTERPOLATION_FAILED. Callers refuse; nothing is
// approximated from missing metadata.
//
// Numeric domain: wavelengths are declared in MICROMETRES (the
// SICNU_SAR_WAVELENGTH_UM contract) and converted once, here, to metres;
// temporal baselines are signed floating days (slave − master); baseline
// components are metres; height ambiguity is metres of height per
// 2π of wrapped topographic phase.
#pragma once

#include "sar_orbit.h"

#include <QString>

#include <limits>

namespace sicnu::sar
{

/// Input-truth schema version of this module (bump on a semantic change of
/// the struct contracts below; consumers stamp it into provenance).
inline constexpr int kInSarTruthVersion = 1;

/// Nanometre-of-truth constant: quiet NaN default so an unset field is
/// detectable (never zero — zero is a valid UTC and a valid-looking length).
inline constexpr double kUnset = std::numeric_limits<double>::quiet_NaN();

/// One interferometric partner scene, versioned input truth.
struct InSarSceneTruth
{
    /// Absolute UTC acquisition time, seconds since the UNIX epoch.
    double acquisitionUtcSec = kUnset;
    /// Radar wavelength in micrometres (> 0). C-band ≈ 55500 µm.
    double wavelengthUm = kUnset;
    /// Optional absolute UTC anchor of the orbit time base (the orbit's
    /// t = 0 second); NaN = not declared (orbit-relative-only base).
    double azimuthStartUtcSec = kUnset;
    /// Orbit states on the declared (scene-relative) base. Must satisfy
    /// OrbitSegment::isValid().
    OrbitSegment orbit;
};

/// Versioned pair truth: two validated partner scenes plus the pair-level
/// derivations that do not depend on any ground point.
struct InSarPairTruth
{
    InSarSceneTruth master;
    InSarSceneTruth slave;
    /// slave.acquisitionUtcSec − master.acquisitionUtcSec, in floating days
    /// (signed; positive = slave acquired after master).
    double temporalDays = 0.0;
    /// True iff both scenes declared finite azimuthStartUtcSec values AND
    /// their absolute orbit windows overlap (informational; the geometry
    /// kernels do not require it).
    bool orbitsShareAbsoluteBase = false;
    /// Scene-truth schema version used to build this pair.
    int truthVersion = kInSarTruthVersion;
};

/// Contract validation of one scene truth: finite UTC, finite wavelength
/// > 0, orbit segment valid. Returns false with a domain-coded @a error
/// otherwise.
bool validateSceneTruth( const InSarSceneTruth &scene, QString *error = nullptr );

/// Builds the versioned pair truth from two scene truths: validates both,
/// checks wavelength consistency (relative difference > 1e-9 is a
/// WAVELENGTH_INCOMPATIBLE refusal — interferometric phase between different
/// radars is not defined), computes the signed temporal baseline, and —
/// when both absolute anchors exist — checks the absolute orbit windows
/// overlap (ORBIT_EPOCH_MISMATCH refusal when they do not: the two
/// acquisitions cannot image a common area).
bool buildPairTruth( const InSarSceneTruth &master, const InSarSceneTruth &slave,
                     InSarPairTruth *out, QString *error = nullptr );

/// One per-ground-point baseline sample: the B∥/B⊥/|Δr| of the pair as
/// seen from @a ground, plus the zero-Doppler ranges (metres) and the
/// orbit-base crossing times of both scenes at that point.
struct PairBaselineSample
{
    double parallelM = 0.0;      ///< B∥ (LOS projection)
    double perpendicularM = 0.0; ///< B⊥ (sign-free magnitude, sar_orbit.h contract)
    double magnitudeM = 0.0;     ///< |Δr|
    double rangeMasterM = kUnset;  ///< zero-Doppler slant range, master
    double rangeSlaveM = kUnset;   ///< zero-Doppler slant range, slave
    double azimuthTimeMaster = kUnset; ///< orbit-base crossing time, master
    double azimuthTimeSlave = kUnset;  ///< orbit-base crossing time, slave
};

/// Rigorous per-ground-point pair baseline: zero-Doppler crossing of BOTH
/// orbits at the ground point (forwardRangeDoppler), sensor positions by
/// orbit interpolation at the crossings, LOS ground→master-sensor, then
/// the interferometricBaseline authority. Refuses (typed, domain-coded)
/// when either orbit has no zero-Doppler crossing at the point or an
/// interpolation fails — the caller never gets a guessed baseline.
bool pairBaselineAtGround( const InSarPairTruth &pair,
                           const GeodeticPoint &ground,
                           PairBaselineSample *out, QString *error = nullptr );

/// Height ambiguity (metres of height per 2π of wrapped topographic
/// phase): h_amb = λ·r·sinθ / (2·B⊥), with @a slantRangeM the slant range,
/// @a incidenceDeg the local incidence angle in degrees, @a wavelengthM
/// the radar wavelength in metres. Returns NaN and refuses via @a error
/// for a degenerate |B⊥| (the pair has no height sensitivity).
double heightAmbiguityM( double perpendicularM, double slantRangeM,
                         double incidenceDeg, double wavelengthM,
                         QString *error = nullptr );

/// Micrometre contract → metres (single conversion point for the whole
/// InSAR family; identity for values already in metres is a caller error).
constexpr double wavelengthUmToM( double wavelengthUm ) { return wavelengthUm * 1e-6; }

} // namespace sicnu::sar
