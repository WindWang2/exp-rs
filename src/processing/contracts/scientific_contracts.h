// scientific_contracts.h — canonical per-raster scientific contracts
// (Foundation 6.0, Milestone B).
//
// Purpose: algorithms must not independently guess scientific semantics
// tile-by-tile (goal §3). Everything that depends on the WHOLE raster —
// numeric domain (DN-scale vs unit reflectance), radiometric state,
// per-band NoData, grid, SAR geometry — is resolved ONCE per dataset
// through the helpers here (or the established kernels they name), is
// deterministic, is logged with its evidence, and is passed explicitly to
// the streaming kernels.
//
// This header is deliberately dependency-light (Qt JSON only, no GDAL) so
// kernels, operators and tests can all consume it.
#pragma once

#include <QString>
#include <QJsonObject>

namespace sicnu::processing::contracts
{

/// Numeric domain of spectral data. Ratio indices are invariant; indices
/// with additive constants (EVI/SAVI/EVI2/MSAVI) are anchored to unit
/// reflectance and need the data scale to compute them correctly.
enum class NumericScaleRegime
{
    UnitReflectance, ///< values live on [0, ~1.6]; constants used verbatim
    DnScale,         ///< digital numbers on [0, ~10000]; values must be
                     ///< divided by `divisor` before the unit-reflectance
                     ///< kernels see them
};

/// The numeric-domain contract resolved ONCE per raster before streaming
/// (#801: deciding per tile produced seams — adjacent tiles could pick
/// different index constants and stripe the output).
struct NumericDomainContract
{
    NumericScaleRegime regime = NumericScaleRegime::UnitReflectance;
    double divisor = 1.0;        ///< stored / divisor → unit reflectance
    QString resolvedBy;          ///< provenance: "declared-metadata",
                                 ///< "dataset-statistics", "default-unit"
    double observedMaxAbs = 0.0; ///< evidence from the resolution pass
                                 ///< (0 when resolution used declarations)

    bool isDnScale() const { return regime == NumericScaleRegime::DnScale; }

    QJsonObject toJson() const;
};

/// The dataset-level decision rule (pure; unit-testable). A declared
/// metadata scale wins outright. Otherwise the magnitude rule: max|value|
/// over a bounded whole-raster sample > kDnScaleThreshold ⇒ DN-scale with
/// the canonical 10000 divisor; anything else stays unit reflectance.
/// The threshold matches the documented heuristic
/// (docs/processing/grid-and-radiometric-policy.md §2) but is evaluated
/// ONCE per dataset, never per tile.
constexpr double kDnScaleThreshold = 5.0;
constexpr double kCanonicalDnDivisor = 10000.0;

NumericDomainContract domainFromDeclaredScale( double declaredScale );
NumericDomainContract domainFromMaxAbsSample( double maxAbsSample );
NumericDomainContract defaultUnitDomain();

} // namespace sicnu::processing::contracts
