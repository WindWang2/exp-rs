// src/agent/spatial_tools/spectral_spatial_tools.h — D13 agent spectral tools
#pragma once

#include "spatial_tool.h"

namespace exp_agent
{

    /// spatial:spectral_inspect — extracts a pixel's full spectral profile,
    /// fits continuum-removal absorption features and (optionally) matches
    /// the spectrum against a reference library:
    ///   input : { layer_id | path, point: [x, y], match_library?: bool,
    ///             library_path?: string }
    ///   output: { wavelengths[], reflectance[], absorption_depth,
    ///             absorption_wavelength_nm, matched_material, confidence }
    class SpectralInspectTool : public sicnu::agent::spatial_tools::SpatialTool
    {
      public:
        std::string name() const override { return "spatial:spectral_inspect"; }
        std::string displayName() const override { return "Inspect Spectral Profile"; }
        std::string description() const override;
        std::vector<std::string> tags() const override;
        Json::Value inputSchema() const override;
        Json::Value outputSchema() const override;
        sicnu::agent::spatial_tools::SpatialToolResult execute( const Json::Value &params ) override;
    };

    /// spatial:validate_boa_physics — physics self-consistency audit of a BOA
    /// reflectance pixel:
    ///   ρ ∈ [0, 1] for every band (UNPHYSICAL_REFLECTANCE_RANGE);
    ///   water-surface rule: ρ(NIR) > ρ(Red) && ρ(NIR) > 0.15
    ///     → INVERTED_WATER_SPECTRUM (default audit — the dominant confusion);
    ///   vegetation rule (expected_surface = "vegetation"):
    ///     ρ(NIR) / ρ(Red) < 2.0 → INVERTED_VEGETATION_RATIO.
    /// Bands are chosen by WAVELENGTH metadata (NIR ≈ 850 nm, Red ≈ 660 nm);
    /// without wavelength metadata the tool refuses (never guesses ordinals).
    class ValidateBoaPhysicsTool : public sicnu::agent::spatial_tools::SpatialTool
    {
      public:
        std::string name() const override { return "spatial:validate_boa_physics"; }
        std::string displayName() const override { return "Validate BOA Physics"; }
        std::string description() const override;
        std::vector<std::string> tags() const override;
        Json::Value inputSchema() const override;
        Json::Value outputSchema() const override;
        sicnu::agent::spatial_tools::SpatialToolResult execute( const Json::Value &params ) override;
    };

} // namespace exp_agent
