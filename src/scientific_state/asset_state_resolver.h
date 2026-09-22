/***************************************************************************
  scientific_state/asset_state_resolver.h
  RS14-01 Scientific Data Passport — the state resolver.

  Pure projection: source-tagged facts in, claim-annotated
  RemoteSensingAssetState out. No I/O, no mutation of inputs, deterministic
  for identical inputs. The resolver owns every vocabulary rule
  (radiometric normalization, wavelength units, band-role fallbacks) so
  adapters stay dumb and behaviour stays testable.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_ASSET_STATE_RESOLVER_H
#define SICNU_SCIENTIFIC_STATE_ASSET_STATE_RESOLVER_H

#include "scientific_state/asset_state_types.h"
#include "scientific_state/state_facts.h"

#include <optional>
#include <string>

namespace sicnu::state
{

struct StateResolutionInput
{
    std::optional<CatalogFacts> catalog;
    std::optional<DatasetFacts> dataset;
    std::optional<SensorProfileFacts> sensorProfile;
    std::optional<DerivationFacts> derivation;
    std::optional<ModelSidecarFacts> modelSidecar;

    /// Identity fallback when neither catalog nor dataset carries a path.
    std::string sourcePath;
};

struct ResolveOutcome
{
    RemoteSensingAssetState state;
};

/// Resolves the unified scientific state. Deterministic; never throws.
ResolveOutcome resolveAssetState( const StateResolutionInput &input );

/// Normalized radiometric vocabulary ("digital_number", "radiance",
/// "toa_reflectance", "surface_reflectance", "brightness_temperature",
/// "sigma0", "gamma0", "beta0"). Returns false for foreign tokens.
bool normalizeRadiometricToken( const std::string &raw, std::string &unit );

/// Normalizes a wavelength to nanometers. Supported units (case-insensitive):
/// "nm" (default when absent) and "µm"/"um"/"micrometer(s)"/"micron(s)".
/// Returns false for unknown units, unparsable or non-finite values.
bool normalizeWavelengthNm( const std::string &rawValue, const std::string &units,
                            double &outNm, bool &unknownUnits );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_RESOLVER_H
