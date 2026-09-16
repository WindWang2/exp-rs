// src/processing/algorithms/radiometric_transition.h — conversion-legality
// authority over the SICNU_RADIOMETRIC_STATE vocabulary
// (radiometric-physics-11, work package A).
//
// ADR 0114 gives the platform a closed five-state vocabulary and file
// metadata helpers (satellite_products.h), but nothing decides whether a
// requested conversion is *physically lawful* for the data at hand, which
// inputs each edge requires, or what was actually applied. Downstream
// consumers (change detection, index math) can only string-compare states,
// so mislabeled rasters flow through until a numeric kernel happens to fail.
//
// This authority owns three things, on top of the existing vocabulary —
// it deliberately introduces NO new unit names (see
// .planning/radiometric-physics-11/DECISIONS.md D1):
//
//   1. The lawful transition DAG (physical, not administrative):
//
//        digital_number ──▶ radiance ──▶ toa_reflectance ──▶ surface_reflectance
//               │                                │
//               └──────────────▶ └──▶ brightness_temperature
//
//      radiance→TOA is the ESUN path (ρ = π·L·d²/(ESUN·cos θz) = π·L/(E₀·ESUN·cos θz)); the DN→TOA
//      edge is the direct coefficient/quantification path. Identity is
//      lawful. Every inversion (SR→TOA, TOA→DN, …) and unit-jumping shortcut
//      (DN→SR, DN→BT, L→SR, TOA→BT, any→BT except via radiance) is unlawful:
//      the removed information cannot be conjured back, and "correcting"
//      reflectance into temperature is a category error, not a conversion.
//
//   2. Per-edge required-input predicates bound to the house
//      RadiometricCalibration::BandCoefficients flags, so "lawful" and
//      "runnable with these coefficients" are distinguished and every refusal
//      names the missing inputs as stable tokens (fail-closed, GOAL Oracle 2).
//
//   3. A versioned provenance record (schema exp_rs_radiometric_provenance/1)
//      describing the planned chain: edges, formula tokens, the coefficients
//      and geometry applied, and the numeric scale before/after — written by
//      operators into results/dataset metadata so every output pixel's unit
//      is traceable (GOAL Oracle 1).
//
// Scaled DN stacks (SICNU_NUMERIC_SCALE, e.g. Sentinel-2 L2A 10000) are an
// import-time concern handled by the grid-and-radiometric policy; the plan
// records the scale but this authority does not model a scaled-DN→reflectance
// edge. Sentinel-2 L2A stacks therefore stay import-stamped, never planned.
#pragma once

#include "radiometric_calibration.h"

#include <QString>
#include <QStringList>

#include <json/json.h>

namespace RadiometricTransition
{

using RadiometricCalibration::BandCoefficients;
using RadiometricCalibration::SensorType;

/// Stable edge tokens (provenance "steps" entries / operator results).
extern const char *const kStepDnToRadiance;
extern const char *const kStepDnToToaReflectance;
extern const char *const kStepRadianceToToaReflectance;
extern const char *const kStepRadianceToBrightnessTemperature;
extern const char *const kStepToaToSurfaceReflectance;

/// Stable missing-input tokens ("missing" entries). Each names ONE thing a
/// caller can actually provide; explanations carry the human prose.
extern const char *const kMissingRadianceCoefficients;
extern const char *const kMissingReflectanceCoefficients;
extern const char *const kMissingSunElevation;
extern const char *const kMissingEsun;
extern const char *const kMissingThermalConstants;
extern const char *const kMissingAtmosphericProvider;

/// True when @p state is one of the five ADR 0114 vocabulary values.
bool isKnownState( const QString &state );

/// True when the direct edge @p from → @p to exists in the lawful DAG
/// (identity returns true). Unknown states are never lawful.
bool isLawfulEdge( const QString &from, const QString &to );

/// Everything the planner may need for one band. Null/absent members simply
/// mean "not provided" and surface as missing tokens, never as defaults.
struct StepInputs
{
    /// Per-band coefficients (RadiometricCalibration::loadMetadata output).
    const BandCoefficients *coeffs = nullptr;
    SensorType sensor = SensorType::Unknown;

    /// ESUN for the radiance→TOA path [W·m⁻²·sr⁻¹·µm⁻¹]; 0 = unavailable.
    double esun = 0.0;

    /// Sun elevation in degrees (metadata OR SolarGeometry-computed).
    /// ≤ 0 / non-finite / !sunElevationKnown = no usable geometry.
    double sunElevationDeg = 0.0;
    bool sunElevationKnown = false;
    /// Provenance detail: true = read from scene metadata, false = derived
    /// from acquisition time via SolarGeometry.
    bool sunElevationFromMetadata = false;

    /// Atmospheric provider id available for TOA→surface (e.g. "dos1",
    /// "dos2", "quac"; empty = none). Aux-input validation is the provider
    /// seam's job (AtmosphericCorrectionProvider); this authority only
    /// requires that SOME provider was selected.
    QString atmosphericProvider;

    /// SICNU_NUMERIC_SCALE of the source stack (1 = physical units).
    double numericScale = 1.0;
};

/// Outcome of planning @p from → @p to for one band.
struct Plan
{
    /// A path of lawful edges exists (false ⇒ category error; `missing` is
    /// empty and `explanation` names the unlawful request).
    bool lawful = false;
    /// lawful && every edge's required inputs are provided.
    bool satisfiable = false;
    /// Edge tokens along the planned path (shortest lawful chain).
    QStringList steps;
    /// Stable tokens for everything missing across the chain (deduplicated,
    /// deterministic order). Empty ⇔ satisfiable.
    QStringList missing;
    /// Human-readable summary / refusal reason.
    QString explanation;
    /// exp_rs_radiometric_provenance/1 record. Present whenever lawful
    /// (also when !satisfiable, so audits can see what WOULD be applied);
    /// meaningless when !lawful.
    Json::Value provenance;
};

/// Plans the shortest lawful chain @p from → @p to for one band and evaluates
/// every edge against @p inputs. Never throws; unknown states/unlawful pairs
/// produce a typed refusal in the Plan (never a default chain).
Plan plan( const QString &fromState, const QString &toState, const StepInputs &inputs );

} // namespace RadiometricTransition
