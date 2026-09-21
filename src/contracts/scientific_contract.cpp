/***************************************************************************
 * scientific_contract.cpp — Scientific Contract Registry (Platform 10.0)
 *
 * The registry data. Authoring discipline:
 *   - One FAMILY base record per operator family, then per-operator rows
 *     that override only what genuinely differs. A family default that is
 *     wrong for a member is a bug: fix the row, never bend the vocabulary.
 *   - Every row carries an `evidence` anchor: a test that pins the claimed
 *     behaviour, a review finding that fixed it, or an explicit
 *     "family:<name> + schema read" for declarations anchored by source
 *     reading at authoring time.
 ***************************************************************************/
#include "scientific_contract.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace sicnu::contracts {

const std::vector<std::string> kNumericDomains = {
    "none",        // no raster surface (vector/table/json tools)
    "any",         // domain-agnostic math (ratios, expressions)
    "dn",          // raw digital numbers / quantized counts-as-stored
    "reflectance", // surface or TOA reflectance (dimensionless, ~[0,1+])
    "radiance",    // radiometric radiance (W/sr/m^2/micron)
    "temperature", // thermal (Kelvin or declared scale)
    "amplitude",   // SAR amplitude
    "sigma0",      // SAR linear-power backscatter coefficient
    "gamma0",      // SAR linear-power gamma coefficient
    "beta0",       // SAR linear-power beta coefficient
    "phase",       // SAR interferometric phase (radians; unwrapped unless declared)
    "displacement",// line-of-sight ground displacement (meters; + toward sensor)
    "db",          // decibel-scaled (any of the above)
    "index",       // normalized spectral index (typically [-1,1])
    "probability", // score in [0,1]
    "mask",        // binary 0/1 (1 = condition true)
    "classes",     // categorical class ids
    "features",    // generic multi-band feature stack
    "count",       // non-negative integer counts
    "vector",      // vector geometry surface
    "table",       // tabular/zonal result surface
};

const std::vector<std::string> kScaleOffsetPolicies = {
    "identity",         // pixels consumed/produced as stored
    "param_driven",     // explicit scale/offset parameters
    "product_metadata", // scale/offset read from product metadata (MTL, GRD, ...)
    "not_applicable",
};

const std::vector<std::string> kNoDataPolicies = {
    "propagate",     // invalid inputs stay invalid on the output
    "read_metadata", // band-declared NoData is honoured/declared explicitly
    "fail_closed",   // unreadable samples are excluded/masked, never trusted
    "internal_sentinel", // a reserved output sentinel marks invalid pixels
    "none",
};

const std::vector<std::string> kCategoricalEncodings = {
    "none",
    "input_preserved",
    "byte_nodata_255",
    "uint16_nodata_65535",
    "uint32_nodata_0",
    "vector_labels",
    // Dtype escalates with the class-id domain; 0 = unclassified/NoData
    // (classification pipeline / RsClassRaster::paint convention).
    "escalating_nodata_0",
    // Byte/255 for <=255 product classes, UInt16/65535 above (the F-OPS-1
    // labels escalation contract shared by rs:infer and rs:segment).
    "byte_uint16_escalating",
    // Labels carried in Float32 (spectral SAM class maps).
    "float32_nodata_-9999",
    // Component labels in Float32 with NaN NoData (exact ids only to 2^24).
    "float32_nodata_nan",
};

const std::vector<std::string> kTimeAlignments = {
    "not_applicable",
    "single_scene",
    "stack_dates",     // consumes a scene stack keyed by acquisition dates
    "increasing_dates",// requires strictly increasing timestamps
    "matched_grid",    // frames must share one grid on top of dates
};

const std::vector<std::string> kWavelengthPolicies = {
    "not_applicable",
    "band_roles",     // requires role-tagged bands (nir/red/...), no SRF
    "srf_or_center",  // requires spectral response functions or center wavelengths
};

const std::vector<std::string> kSeedPolicies = {
    "none",
    "deterministic_internal", // seeded deterministically inside the kernel
    "seed_param",             // caller-provided seed parameter
};

const std::vector<std::string> kCancellationGranularities = {
    "not_applicable",
    "operator_level",  // checked between whole-raster phases
    "step_level",      // checked between pipeline steps / scene frames
    "tile_level",      // checked between tiles
    "row_block_level", // checked between streaming row blocks
};

const std::vector<std::string> kAtomicPublications = {
    "no_partial_output", // the output path never holds a partial raster (#647)
    "staged_rename",     // same-dir stage + rename publish (stronger, verified)
    "json_result_only",  // result is the JSON payload; nothing written
    "direct_write",      // writes in place (documented exceptions only)
};

const std::vector<std::string> kProvenanceExpectations = {
    "none",
    "output_metadata",    // key parameters/state recorded on the output raster
    "derivation_record",  // full structured derivation recorded downstream
};

namespace {

bool inVocabulary( const std::string &value, const std::vector<std::string> &vocabulary )
{
    return std::find( vocabulary.begin(), vocabulary.end(), value ) != vocabulary.end();
}

/// Family bases --------------------------------------------------------------

ScientificContract baseRecord()
{
    ScientificContract c;
    c.inputDomain = "any";
    c.outputDomain = "features";
    c.scaleOffset = "identity";
    c.noDataPolicy = "propagate";
    c.categoricalEncoding = "none";
    c.classIdRange = "";
    c.timeAlignment = "single_scene";
    c.wavelengthPolicy = "not_applicable";
    c.seedPolicy = "none";
    c.cancellationGranularity = "operator_level";
    c.atomicPublication = "no_partial_output";
    c.provenance = "none";
    c.refusalCodes = { "InvalidParameter", "MissingRequiredParameter" };
    c.evidence = "";
    return c;
}

/// Normalized-ratio spectral index family: scientifically meaningful on
/// reflectance surfaces but numerically defined for any comparable bands.
ScientificContract indexFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "any";
    c.outputDomain = "index";
    c.wavelengthPolicy = "band_roles";
    c.noDataPolicy = "propagate";
    c.evidence = "family:spectral-index + schema read";
    return c;
}

ScientificContract sarFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "dn";
    c.scaleOffset = "product_metadata";
    c.evidence = "family:sar + schema read";
    return c;
}

ScientificContract classificationFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "features";
    c.outputDomain = "classes";
    // RsClassificationPipeline escalates Byte -> UInt16 -> Int32 keyed on the
    // max class id, with the unclassified value (default 0) as NoData — NOT
    // the Byte/255 sentinel this family claimed before the honesty review.
    c.categoricalEncoding = "escalating_nodata_0";
    c.classIdRange = "1..2147483646";
    c.evidence = "family:classification + pipeline dtype escalation review";
    return c;
}

ScientificContract changeFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "reflectance";
    c.timeAlignment = "stack_dates";
    c.evidence = "family:change + schema read";
    return c;
}

ScientificContract temporalFamily( const std::string &dates = "stack_dates" )
{
    ScientificContract c = baseRecord();
    c.inputDomain = "any";
    c.timeAlignment = dates;
    c.cancellationGranularity = "step_level";
    c.evidence = "family:temporal + schema read";
    return c;
}

ScientificContract importFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "none";
    c.outputDomain = "dn";
    c.scaleOffset = "product_metadata";
    c.timeAlignment = "single_scene";
    c.cancellationGranularity = "step_level";
    c.provenance = "output_metadata";
    c.evidence = "family:import + schema read";
    return c;
}

ScientificContract filterFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "any";
    c.evidence = "family:filter + schema read";
    return c;
}

ScientificContract fusionFamily()
{
    ScientificContract c = baseRecord();
    c.inputDomain = "reflectance";
    c.outputDomain = "reflectance";
    c.evidence = "family:fusion + schema read";
    return c;
}

/// I/O foundation family (Platform 11.0 census 2.0): thin JSON adapters over
/// the Qt-free sicnu_geospatial core whose kernels publish through
/// atomic_fs::writeFileAtomic ("validated and published atomically",
/// io_operators.cpp/io_fabric_operators.cpp). Scientific semantics: domains
/// pass through unchanged; NoData follows band metadata; conversion kernels
/// are deterministic copies (resampling seams declare tolerance).
ScientificContract ioFamily( const std::string &input = "any",
                             const std::string &output = "any" )
{
    ScientificContract c = baseRecord();
    c.inputDomain = input;
    c.outputDomain = output;
    c.noDataPolicy = "read_metadata";
    c.atomicPublication = "staged_rename";
    c.provenance = "output_metadata";
    c.refusalCodes = { "InvalidParameter", "MissingRequiredParameter", "FileNotFound",
                       "FileNotReadable", "FileNotWritable", "InvalidInputData",
                       "GdalError", "Cancelled" };
    c.evidence = "family:io + schema read (io_operators.h/io_fabric_operators.h)";
    return c;
}

/// Map-cartography agent family (Platform 11.0 census 2.0): MapSpec-driven
/// GUI-cartography agents. compose/validate/preflight/repair return JSON
/// verdicts and never write rasters; export publishes temp → verify →
/// sha256 → rename (cartography_operators.cpp export operator).
ScientificContract cartographyFamily( const std::string &publication )
{
    ScientificContract c = baseRecord();
    c.inputDomain = "any";
    c.outputDomain = "none";
    c.atomicPublication = publication;
    c.refusalCodes = { "InvalidParameter", "MissingRequiredParameter",
                       "InvalidInputData", "NotInitialized", "Cancelled" };
    c.evidence = "family:cartography + schema read (cartography_operators.cpp)";
    return c;
}

} // namespace

const std::map<std::string, ScientificContract> &scientificContracts()
{
    static const std::map<std::string, ScientificContract> table = [] {
        std::vector<ScientificContract> rows;

        // --- Spectral indexes ------------------------------------------------
        for ( const char *id : { "rs:ndvi", "rs:evi", "rs:ndwi", "rs:mndwi", "rs:savi", "rs:ndbi",
                                 "rs:spectral_index" } )
        {
            ScientificContract c = indexFamily();
            c.operatorId = id;
            rows.push_back( c );
        }
        {
            ScientificContract c = indexFamily();
            c.operatorId = "rs:band_ratio";
            c.wavelengthPolicy = "not_applicable"; // explicit band numbers
            rows.push_back( c );
        }
        {
            ScientificContract c = indexFamily();
            c.operatorId = "rs:band_math";
            c.outputDomain = "features";
            c.wavelengthPolicy = "not_applicable"; // arbitrary expression
            c.evidence = "family:spectral-index; expression is domain-agnostic";
            rows.push_back( c );
        }

        // --- Spectral tools ----------------------------------------------------
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:continuum_removal";
            c.inputDomain = "reflectance";
            c.outputDomain = "reflectance";
            c.wavelengthPolicy = "band_roles";
            c.evidence = "family:spectral + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:spectral_derivative";
            c.inputDomain = "reflectance";
            c.outputDomain = "features";
            c.evidence = "family:spectral + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:spectral_resample";
            c.inputDomain = "reflectance";
            c.outputDomain = "reflectance";
            c.wavelengthPolicy = "srf_or_center";
            c.evidence = "family:spectral; resampling needs SRF/center wavelengths";
            rows.push_back( c );
        }
        {
            // Matched filter / ACE write ONE continuous detection-score band
            // (Float32, NaN NoData) — a probability-like surface, not classes.
            for ( const char *id : { "rs:matched_filter", "rs:ace", "rs:cem_detection" } )
            {
                ScientificContract c = baseRecord();
                c.operatorId = id;
                c.inputDomain = "reflectance";
                c.outputDomain = "probability";
                c.noDataPolicy = "internal_sentinel";
                c.wavelengthPolicy = "srf_or_center";
                c.evidence = "review:spectral detection writer reads (Float32 scores)";
                rows.push_back( c );
            }
            {
                // Score fusion consumes detection-score planes
                // (probability-like, NaN NoData) and writes the same kind of
                // surface (Spectral Intelligence 12.0).
                ScientificContract fuse = baseRecord();
                fuse.operatorId = "rs:spectral_spatial_fuse";
                fuse.inputDomain = "probability";
                fuse.outputDomain = "probability";
                fuse.noDataPolicy = "internal_sentinel";
                fuse.evidence = "review:spectral-spatial fusion reads and writes Float32 "
                                "score planes with NaN NoData";
                rows.push_back( fuse );
            }
            // SAM classifies in Float32 label space with -9999 NoData.
            ScientificContract sam = baseRecord();
            sam.operatorId = "rs:sam_classify";
            sam.inputDomain = "reflectance";
            sam.outputDomain = "classes";
            sam.categoricalEncoding = "float32_nodata_-9999";
            sam.wavelengthPolicy = "srf_or_center";
            sam.evidence = "review:rs_sam_classify writer reads (Float32 labels, -9999)";
            rows.push_back( sam );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:spectral_unmixing";
            c.inputDomain = "reflectance";
            c.outputDomain = "features"; // abundance stacks, continuous
            c.wavelengthPolicy = "srf_or_center";
            c.evidence = "family:spectral + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:rx_anomaly";
            c.inputDomain = "reflectance";
            c.outputDomain = "probability";
            c.evidence = "family:spectral + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:endmember_extraction";
            c.inputDomain = "reflectance";
            c.outputDomain = "table";
            c.wavelengthPolicy = "srf_or_center";
            c.atomicPublication = "json_result_only";
            c.evidence = "family:spectral; result is an endmember table (no file writes)";
            rows.push_back( c );
        }
        {
            // Spectral Intelligence 11.0 family — contract rows were missing
            // at the adf8f989 baseline (census gate red).
            ScientificContract c = baseRecord();
            c.operatorId = "rs:local_rx_anomaly";
            c.inputDomain = "reflectance";
            c.outputDomain = "probability";
            c.noDataPolicy = "internal_sentinel";
            c.evidence = "family:spectral + ADR 0163 (dual-window RX quality planes; "
                         "unscored pixels stay NaN)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:spectral_similarity";
            c.inputDomain = "reflectance";
            c.outputDomain = "probability";
            c.wavelengthPolicy = "srf_or_center";
            c.evidence = "family:spectral + ADR 0163 (bounded ProductNormalized hybrid; "
                         "classic_tan is unbounded by design)";
            c.note = "classic_tan form is unbounded; the default form is in [0, 1]";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:sparse_unmixing";
            c.inputDomain = "reflectance";
            c.outputDomain = "features"; // abundance stacks, continuous
            c.wavelengthPolicy = "srf_or_center";
            c.evidence = "family:spectral + ADR 0163 (FISTA, collinear-atom refusal)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:endmember_analysis";
            c.inputDomain = "reflectance";
            c.outputDomain = "table";
            c.wavelengthPolicy = "srf_or_center";
            c.evidence = "family:spectral + ADR 0163 (derived spectral-table artifact; "
                         "sensor projection refuses without wavelength metadata)";
            rows.push_back( c );
        }

        // --- Atmospheric / radiometric ----------------------------------------
        for ( const char *id :
              { "rs:atmospheric_correction", "rs:atmospheric_dos1", "rs:atmospheric_dos2",
                "rs:atmospheric_quac" } )
        {
            ScientificContract c = baseRecord();
            c.operatorId = id;
            c.inputDomain = "dn"; // dn_to_radiance is a first-class method
            c.outputDomain = "reflectance";
            c.evidence = "review:atmospheric header reads (converts DN; radiance/reflectance out)";
            c.note = "output domain follows the declared method (radiance or surface reflectance)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:radiometric_calibration";
            c.inputDomain = "dn";
            c.outputDomain = "radiance"; // or reflectance per declared target
            c.scaleOffset = "product_metadata";
            c.evidence = "family:radiometric + schema read";
            c.note = "output domain follows the declared calibration target";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:brdf_normalization";
            c.inputDomain = "reflectance";
            c.outputDomain = "reflectance";
            c.evidence = "family:radiometric + schema read (sun/view geometry "
                         "normalization to a reference geometry)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:dn_to_radiance";
            c.inputDomain = "dn";
            c.outputDomain = "radiance";
            c.scaleOffset = "param_driven";
            c.evidence = "family:radiometric + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:radiometric_qa";
            c.inputDomain = "any";
            c.outputDomain = "mask"; // uint16 flag raster, 0 = clean
            c.provenance = "output_metadata";
            c.evidence = "family:radiometric + schema read (per-band QA flag raster)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:solar_geometry";
            c.inputDomain = "any";
            c.outputDomain = "features"; // sun elevation/azimuth, declination, distance
            c.evidence = "family:radiometric + schema read (ephemeris feature bands)";
            rows.push_back( c );
        }

        // --- Change detection --------------------------------------------------
        for ( const char *id :
              { "rs:change_cva", "rs:change_cva_angle", "rs:change_difference",
                "rs:change_log_ratio", "rs:change_mad", "rs:change_normalized_difference",
                "rs:change_ratio", "rs:change_sam", "rs:change_detection", "rs:change_irmad" } )
        {
            ScientificContract c = changeFamily();
            c.operatorId = id;
            if ( std::string( id ) == "rs:change_irmad" || std::string( id ) == "rs:change_mad" )
            {
                c.outputDomain = "features"; // variate stacks + changed labels
                c.noDataPolicy = "propagate";
            }
            rows.push_back( c );
        }
        {
            ScientificContract c = changeFamily();
            c.operatorId = "rs:post_classification_change";
            c.inputDomain = "classes";
            c.outputDomain = "classes";
            c.categoricalEncoding = "uint16_nodata_65535"; // change-type codes
            c.classIdRange = "";
            c.evidence = "review:change-map writer reads (UInt16 codes, 65535 sentinel)";
            c.note = "change-type codes in UInt16; class_count param caps at 255";
            rows.push_back( c );
        }

        // --- SAR ------------------------------------------------------------------
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_calibrate";
            c.outputDomain = "sigma0"; // declared outputDomain param; sigma0 default
            c.evidence = "review:schema read (sigma0 = DN^2/A^2, outputDomain param)";
            c.note = "outputDomain param selects linear_power|db; the coefficient is always sigma0";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_backscatter";
            c.outputDomain = "gamma0";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_ratio";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_change";
            c.outputDomain = "probability";
            c.timeAlignment = "stack_dates";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:sar_speckle", "rs:sar_texture" } )
        {
            ScientificContract c = sarFamily();
            c.operatorId = id;
            c.outputDomain = "amplitude";
            c.noDataPolicy = "read_metadata"; // #803: per-band sentinel parsing
            rows.push_back( c );
        }
        for ( const char *id :
              { "rs:sar_terrain_correction", "rs:sar_terrain_flatten", "rs:sar_geocode" } )
        {
            ScientificContract c = sarFamily();
            c.operatorId = id;
            c.outputDomain = "sigma0";
            c.evidence = "family:sar + radiometric-terrain semantics";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_terrain_masks";
            c.outputDomain = "mask";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_dualpol_features";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_temporal_stats";
            c.outputDomain = "features";
            c.timeAlignment = "stack_dates";
            c.cancellationGranularity = "step_level";
            rows.push_back( c );
        }

        // --- QA / mask ---------------------------------------------------------------
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:qa_mask";
            c.inputDomain = "any";
            c.outputDomain = "mask";
            c.noDataPolicy = "fail_closed";
            c.cancellationGranularity = "row_block_level";
            c.provenance = "output_metadata";
            c.refusalCodes = { "InvalidParameter", "MissingRequiredParameter", "FileNotFound",
                               "GdalError", "FileNotWritable" };
            c.evidence = "test:test_qa_mask/f-ops-3 + review:F-OPS-3";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:apply_mask";
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.noDataPolicy = "internal_sentinel";
            c.evidence = "family:mask + schema read";
            rows.push_back( c );
        }

        // --- Enhancement / geometry of the raster grid -----------------------------
        for ( const char *id : { "rs:image_enhancement", "rs:contrast_stretch" } )
        {
            ScientificContract c = baseRecord();
            c.operatorId = id;
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.evidence = "family:enhancement + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:resample";
            c.evidence = "family:grid + schema read";
            c.inputDomain = "any";
            c.outputDomain = "any";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:align";
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.timeAlignment = "matched_grid";
            c.evidence = "family:grid + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:mosaic";
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.timeAlignment = "not_applicable"; // input is an array of scenes
            c.evidence = "family:mosaic + schema read";
            rows.push_back( c );
        }
        {
            // F15 mosaic-fusion-11 (ADR 0163): quality mosaic — co-registered
            // scenes only; fail-closed on CRS/pixel-grid mismatch; provenance
            // band traces the dominant contributing input per pixel.
            ScientificContract c = baseRecord();
            c.operatorId = "rs:quality_mosaic";
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.timeAlignment = "not_applicable"; // input is an array of scenes
            c.evidence = "family:mosaic + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:extract_bands";
            c.inputDomain = "any";
            c.outputDomain = "any";
            c.evidence = "family:bands + schema read";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:image_fusion", "rs:fusion_brovey", "rs:fusion_gram_schmidt",
                                 "rs:fusion_ihs", "rs:fusion_linear", "rs:fusion_pca" } )
        {
            ScientificContract c = fusionFamily();
            c.operatorId = id;
            rows.push_back( c );
        }
        // Registration operators REWRITE the moving scene's grid onto the
        // reference grid — geometric alignment, not spectral fusion.
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:register_images";
            c.inputDomain = "any";
            c.outputDomain = "any"; // moving scene resampled onto the reference grid
            c.evidence = "family:registration + schema read (cross-modal "
                         "optical-SAR alignment with refusal semantics)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:stack_register";
            c.inputDomain = "any";
            c.outputDomain = "any"; // globally-registered stack rasters
            c.timeAlignment = "stack_dates";
            c.evidence = "family:registration + schema read (multi-scene "
                         "translation least squares)";
            rows.push_back( c );
        }

        // --- Feature engineering -----------------------------------------------------
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:feature_stack";
            c.inputDomain = "any";
            c.outputDomain = "features";
            c.evidence = "family:features + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:feature_normalize";
            c.inputDomain = "features";
            c.outputDomain = "features";
            c.evidence = "family:features + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:feature_select";
            c.inputDomain = "features";
            c.outputDomain = "features";
            c.evidence = "family:features + schema read";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:pca", "rs:mnf" } )
        {
            ScientificContract c = baseRecord();
            c.operatorId = id;
            c.inputDomain = "features";
            c.outputDomain = "features";
            c.evidence = "family:components + schema read";
            rows.push_back( c );
        }

        // --- Classification / OBIA / label filters ------------------------------------
        for ( const char *id : { "rs:supervised_classification", "rs:obia_classify" } )
        {
            ScientificContract c = classificationFamily();
            c.operatorId = id;
            c.seedPolicy = "deterministic_internal";
            c.evidence = "family:classification + pipeline seam";
            rows.push_back( c );
        }
        {
            // Plain kmeans delegates to cv::kmeans (best-of-3 on the
            // advancing thread-local RNG): cluster ids may permute across
            // runs in one process, so no seed policy is honestly claimable.
            // algorithm=isodata seeds deterministically (even-row) — pin
            // isodata when a replayable class map is required.
            ScientificContract c = classificationFamily();
            c.operatorId = "rs:kmeans_classification";
            c.seedPolicy = "none";
            c.evidence = "review:cv::kmeans attempts=3 RNG semantics + replay test";
            c.note = "algorithm=isodata is the deterministic even-row-seeded variant";
            rows.push_back( c );
        }
        {
            ScientificContract c = classificationFamily();
            c.operatorId = "rs:obia_segment";
            c.outputDomain = "classes"; // segment labels
            c.categoricalEncoding = "uint32_nodata_0";
            c.classIdRange = "1..4294967294";
            c.seedPolicy = "deterministic_internal";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:obia_features", "rs:obia_hierarchy", "rs:obia_label" } )
        {
            ScientificContract c = classificationFamily();
            c.operatorId = id;
            c.inputDomain = "classes";
            if ( std::string( id ) == "rs:obia_features" )
                c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:threshold_raster";
            c.inputDomain = "any";
            c.outputDomain = "mask";
            c.noDataPolicy = "internal_sentinel";
            c.evidence = "family:threshold; 255 marks NoData in the output mask";
            rows.push_back( c );
        }
        {
            ScientificContract c = classificationFamily();
            c.operatorId = "rs:recode";
            c.inputDomain = "classes";
            c.evidence = "review:recode writer reads (Byte/UInt16/Int32 by output range)";
            c.note = "Int32 escalation; negative labels representable";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:sieve", "rs:fill_holes", "rs:majority_filter" } )
        {
            ScientificContract c = classificationFamily();
            c.operatorId = id;
            c.inputDomain = "classes";
            c.categoricalEncoding = "input_preserved";
            c.classIdRange = "";
            rows.push_back( c );
        }
        {
            ScientificContract c = filterFamily();
            c.operatorId = "rs:connected_components";
            c.outputDomain = "classes";
            c.categoricalEncoding = "float32_nodata_nan";
            c.classIdRange = "1..16777216";
            c.evidence = "review:connected-components writer reads (Float32 labels, NaN)";
            c.note = "labels are int ids stored in Float32: exact only up to 2^24 components";
            rows.push_back( c );
        }
        {
            ScientificContract c = filterFamily();
            c.operatorId = "rs:morphology";
            c.inputDomain = "mask";
            c.outputDomain = "mask";
            rows.push_back( c );
        }
        {
            ScientificContract c = filterFamily();
            c.operatorId = "rs:focal_stats";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = filterFamily();
            c.operatorId = "rs:local_extrema";
            c.outputDomain = "mask";
            rows.push_back( c );
        }
        {
            ScientificContract c = filterFamily();
            c.operatorId = "rs:proximity";
            c.outputDomain = "features";
            rows.push_back( c );
        }

        // --- Zonal / vector interop ----------------------------------------------------
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:zonal_stats";
            c.inputDomain = "any";
            c.outputDomain = "table";
            c.atomicPublication = "direct_write"; // CSV result file
            c.evidence = "family:zonal + writer review (QFile CSV output)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:segment_stats";
            c.inputDomain = "classes";
            c.outputDomain = "table";
            c.atomicPublication = "direct_write"; // CSV result file
            c.evidence = "family:zonal + writer review (QFile CSV output)";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:rasterize";
            c.inputDomain = "vector";
            c.outputDomain = "any"; // burns a numeric attribute (Float32, NaN NoData)
            c.evidence = "family:vector-interop + writer review (attribute grid)";
            rows.push_back( c );
        }

        // --- Terrain ----------------------------------------------------------------------
        for ( const char *id : { "rs:terrain_analysis", "rs:terrain_flow",
                                 "rs:terrain_landform", "rs:terrain_solar" } )
        {
            ScientificContract c = baseRecord();
            c.operatorId = id;
            c.inputDomain = "any"; // DEM; accepted in any declared vertical scale
            c.outputDomain = "features";
            c.evidence = "family:terrain + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:topographic_correction";
            c.inputDomain = "reflectance";
            c.outputDomain = "reflectance";
            c.evidence = "family:terrain + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:modis_georeference";
            c.inputDomain = "dn";
            c.outputDomain = "dn";
            c.evidence = "family:import-georef + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:terrain_viewshed";
            c.inputDomain = "any"; // DEM
            c.outputDomain = "mask"; // line-of-sight visibility 0/1
            c.evidence = "family:terrain + schema read (viewshed mask)";
            rows.push_back( c );
        }

        // --- Temporal ------------------------------------------------------------------------
        for ( const char *id : { "rs:temporal_composite", "rs:temporal_summary",
                                 "rs:temporal_index_series" } )
        {
            ScientificContract c = temporalFamily();
            c.operatorId = id;
            c.outputDomain = "features";
            rows.push_back( c );
        }
        for ( const char *id : { "rs:temporal_trend", "rs:temporal_sen_trend",
                                 "rs:temporal_harmonic_fit", "rs:temporal_decompose",
                                 "rs:temporal_anomaly", "rs:temporal_monitor" } )
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = id;
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_breakpoints";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_phenology";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            // Phenology 2.0 (automatic cycles, cross-year, quality flags) —
            // same feature family as rs:temporal_phenology; the contract row
            // was missing at the adf8f989 baseline (census gate red).
            // TI 11.0 phenology 2.0 operators (census coverage repair).
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_phenology_multi";
            c.outputDomain = "features"; // per-cycle phenology metrics
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_gap_fill";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            // Temporal Phenology 12.0: fuses two ALREADY-coregistered feature
            // stacks on one verified pixel grid; provenance recorded on the
            // output (SICNU_FUSION_* keys), never realigns.
            ScientificContract c = temporalFamily( "matched_grid" );
            c.operatorId = "rs:temporal_sar_fusion";
            c.outputDomain = "features";
            c.provenance = "output_metadata";
            c.evidence = "checkGridCompatibility + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_smooth";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_extract_series";
            c.outputDomain = "table";
            c.atomicPublication = "direct_write"; // CSV result file
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_seasonal_breaks";
            c.outputDomain = "features";
            rows.push_back( c );
        }
        {
            ScientificContract c = temporalFamily( "increasing_dates" );
            c.operatorId = "rs:temporal_model_select";
            c.outputDomain = "features"; // per-pixel winner model id + score bands
            c.evidence = "family:temporal + schema read (AICc/BIC/CV model grid)";
            rows.push_back( c );
        }

        // --- Product imports ------------------------------------------------------------------
        for ( const char *id : { "rs:landsat_import", "rs:sentinel2_import", "rs:modis_import",
                                 "rs:gaofen_import", "rs:zy3_import", "rs:hj_import" } )
        {
            ScientificContract c = importFamily();
            c.operatorId = id;
            c.note = "output domain is the product's stored levels; scale/offset stay in metadata";
            rows.push_back( c );
        }

        // --- Model runtime surface ---------------------------------------------------------------
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:infer";
            c.inputDomain = "features";
            c.outputDomain = "features";
            // Labels output encoding: Byte/255 below 256 product classes,
            // UInt16/65535 above — the F-OPS-1 escalation contract.
            c.categoricalEncoding = "byte_uint16_escalating";
            c.classIdRange = "0..65534";
            c.cancellationGranularity = "tile_level";
            c.atomicPublication = "staged_rename";
            c.provenance = "output_metadata";
            c.evidence = "test:test_model_runtime_8/f-ops-1 + tile engine seam";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:segment";
            c.inputDomain = "features";
            c.outputDomain = "classes";
            c.categoricalEncoding = "byte_uint16_escalating";
            c.classIdRange = "0..65534";
            c.cancellationGranularity = "tile_level";
            c.atomicPublication = "staged_rename";
            c.provenance = "output_metadata";
            c.evidence = "family:model-runtime + schema read";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:detect";
            c.inputDomain = "features";
            c.outputDomain = "vector";
            c.cancellationGranularity = "tile_level";
            c.atomicPublication = "staged_rename";
            c.provenance = "output_metadata";
            c.evidence = "test:test_model_tasks/f-ops-5 + detection engine seam";
            rows.push_back( c );
        }
        {
            ScientificContract c = baseRecord();
            c.operatorId = "rs:embedding";
            c.inputDomain = "features";
            c.outputDomain = "features";
            c.cancellationGranularity = "tile_level";
            c.atomicPublication = "json_result_only";
            c.provenance = "output_metadata";
            c.evidence = "family:model-runtime + schema read";
            rows.push_back( c );
        }

        // --- InSAR displacement (Advanced SAR 10.0 package C, D-009) --------
        // Registered but never given its record — caught by the 11.0 census
        // coverage gate (contract-or-exemption over every live id).
        {
            ScientificContract c = sarFamily();
            c.operatorId = "rs:sar_displacement";
            c.inputDomain = "phase";
            c.outputDomain = "displacement";
            c.noDataPolicy = "propagate";
            c.evidence = "family:sar + schema read (rs_sar_displacement_operator.h: "
                         "d_los = -lambda*phi/(4pi), typed refusal on missing "
                         "wavelength, honest Itoh-discontinuity warning gate)";
            c.note = "consumes UNWRAPPED phase; wrapped input is warnable, not "
                     "detectable (D-009 honesty gate)";
            rows.push_back( c );
        }

        // --- Census coverage sweep (Platform 11.0): 17 registered rs:
        // operators that predate their records — each caught live by the
        // census contract-or-exemption gate, each declared from a schema
        // read of its own header. ------------------------------------------------
        {
            // Model-task operators (rs_model_task_operators.h, D19 family):
            // each runs a single forward pass over ONE scene window and
            // publishes a TYPED JSON artifact (exp-rs-classification/1 etc.),
            // not a raster — review F-03 corrected the first-draft record.
            for ( const char *id : { "rs:classify", "rs:regress", "rs:change" } )
            {
                ScientificContract c = baseRecord();
                c.operatorId = id;
                c.inputDomain = "features";
                c.outputDomain = "none";
                c.cancellationGranularity = "operator_level";
                c.atomicPublication = "staged_rename";
                c.provenance = "output_metadata";
                c.evidence = "family:model-runtime + schema read "
                             "(rs_model_task_operators.h: single-pass, typed JSON "
                             "artifact exp-rs-classification/1 family)";
                c.note = "product is a typed JSON artifact, not a categorical raster";
                rows.push_back( c );
            }
            {
                ScientificContract c = importFamily();
                c.operatorId = "rs:cn_product_import";
                c.evidence = "family:import + schema read (rs_cn_product_import_operator.h)";
                rows.push_back( c );
            }
            {
                ScientificContract c = baseRecord();
                c.operatorId = "rs:library_select";
                c.inputDomain = "none";
                c.outputDomain = "table"; // subset library artifact (JSON)
                c.atomicPublication = "direct_write"; // subset.save; atomicity unverified
                c.evidence = "family:spectral-tools + schema read "
                             "(rs_library_select_operator.h: requires output, "
                             "persists the subset library via subset.save)";
                rows.push_back( c );
            }
            {
                ScientificContract c = indexFamily();
                c.operatorId = "rs:mnf_inverse";
                c.inputDomain = "features";
                c.outputDomain = "reflectance";
                c.evidence = "family:spectral-transform + schema read "
                             "(rs_mnf_inverse_operator.h: inverse MNF back-projection)";
                rows.push_back( c );
            }
            {
                ScientificContract c = indexFamily();
                c.operatorId = "rs:spectral_band_select";
                c.outputDomain = "any"; // subset COPY — input domain passes through
                c.wavelengthPolicy = "srf_or_center";
                c.evidence = "family:spectral-tools + schema read "
                             "(rs_spectral_band_select_operator.h: select/exclude "
                             "bands producing the same bands; wavelength-metadata mode "
                             "requires srf_or_center)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_coregister";
                c.inputDomain = "amplitude";
                c.outputDomain = "amplitude"; // resampled complex slave (CFloat32)
                c.evidence = "family:sar + schema read (rs_sar_coregister_operator.h: "
                             "residual global-shift estimate + resampled slave raster; "
                             "reportOnly=1 limits the run to the shift JSON)";
                c.note = "writes the resampled complex slave unless reportOnly=1; "
                         "streaming output stages before publish";
                rows.push_back( c );
            }
            for ( const char *id : { "rs:sar_interferogram", "rs:sar_phase_filter",
                                     "rs:sar_unwrap" } )
            {
                ScientificContract c = sarFamily();
                c.operatorId = id;
                c.inputDomain = "phase";
                c.outputDomain = "phase";
                c.timeAlignment = "stack_dates";
                c.evidence = "family:sar + schema read (InSAR chain: interferogram "
                             "s1*conj(s2) + coherence, Goldstein-Werner filtering, "
                             "quality-guided flood-fill unwrap)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_polsar_decompose";
                c.inputDomain = "amplitude";
                c.outputDomain = "features";
                c.evidence = "family:sar + schema read (rs_sar_polsar_decompose_operator.h: "
                             "Pauli / H-A-alpha / Freeman-Durden components)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_temporal_events";
                c.inputDomain = "amplitude";
                c.outputDomain = "features";
                c.timeAlignment = "stack_dates";
                c.evidence = "family:sar + schema read (rs_sar_temporal_events_operator.h: "
                             "per-pixel change-event dating across N co-registered scenes)";
                rows.push_back( c );
            }
            // Advanced InSAR 11.0 quartet (census coverage repair).
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_coregister_local";
                c.inputDomain = "phase";
                c.outputDomain = "features"; // local offset field between SLC pair
                c.timeAlignment = "matched_grid";
                c.evidence = "family:sar + schema read (same-grid SLC offset field)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_remove_topographic_phase";
                c.inputDomain = "phase";
                c.outputDomain = "phase";
                c.timeAlignment = "matched_grid";
                c.evidence = "family:sar + schema read (DEM/orbit topo phase removal)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_pair_network";
                c.inputDomain = "phase";
                c.outputDomain = "table"; // pair-network JSON artifact
                c.timeAlignment = "stack_dates";
                c.evidence = "family:sar + schema read (baseline-constrained pair plan)";
                rows.push_back( c );
            }
            {
                ScientificContract c = sarFamily();
                c.operatorId = "rs:sar_network_inversion";
                c.inputDomain = "phase";
                c.outputDomain = "displacement"; // per-epoch LOS displacement + velocity
                c.timeAlignment = "stack_dates";
                c.evidence = "family:sar + schema read (network inversion for "
                             "epoch displacement and linear velocity)";
                rows.push_back( c );
            }
            // Temporal family (Temporal Platform 10.0 operators):
            for ( const char *id : { "rs:temporal_extract_regions",
                                     "rs:temporal_harmonic_breaks",
                                     "rs:temporal_region_features",
                                     "rs:temporal_regularize" } )
            {
                ScientificContract c = temporalFamily();
                c.operatorId = id;
                c.outputDomain = id == std::string( "rs:temporal_extract_regions" )
                                       || id == std::string( "rs:temporal_region_features" )
                                     ? "table"
                                     : "features";
                if ( id == std::string( "rs:temporal_regularize" ) )
                    c.provenance = "output_metadata"; // valid_count + filled_count bands
                c.evidence = "family:temporal + schema read (Temporal Platform 10.0: "
                             "CHANGELOG [Unreleased] operators; headers read)";
                rows.push_back( c );
            }
        }

        // --- I/O foundation (census 2.0: first-party io: operators) ---------
        for ( const char *id : { "io:translate", "io:convert_format" } )
        {
            ScientificContract c = ioFamily();
            c.operatorId = id;
            c.evidence = "family:io + schema read (io_operators.h: deterministic "
                         "conversion kernels through the geospatial core)";
            rows.push_back( c );
        }
        for ( const char *id : { "io:warp", "io:reproject" } )
        {
            ScientificContract c = ioFamily();
            c.operatorId = id;
            c.evidence = "family:io + schema read (io_operators.h: float resampling "
                         "through GDAL warp kernels)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily();
            c.operatorId = "io:clip";
            c.evidence = "family:io + schema read (io_operators.h: source-CRS crop; "
                         "refuses CRS-less datasets without a declared fallback)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily();
            c.operatorId = "io:build_overviews";
            c.atomicPublication = "direct_write"; // gdaladdo semantics: in-place derived data
            c.note = "in-place overview building is the documented gdaladdo contract";
            c.evidence = "family:io + schema read (io_operators.h: in-place derived data)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily();
            c.operatorId = "io:make_cog";
            c.evidence = "family:io + schema read (io_operators.h: COG driver + safe "
                         "preset + pre-publish validation)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "vector", "vector" );
            c.operatorId = "io:vector_convert";
            c.evidence = "family:io + schema read (io_operators.h: streaming "
                         "reader→writer vector contract)";
            rows.push_back( c );
        }
        for ( const char *id : { "io:inspect", "io:doctor" } )
        {
            ScientificContract c = ioFamily( "any", "none" );
            c.operatorId = id;
            c.atomicPublication = "json_result_only"; // read-only diagnostics
            c.provenance = "none";
            c.evidence = "family:io + schema read (io_operators.h: read-only, no full scan)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "none", "table" );
            c.operatorId = "io:catalog_search";
            c.atomicPublication = "json_result_only";
            c.evidence = "family:io + schema read (io_fabric_operators.h: STAC/catalog "
                         "query behind one CatalogQuery vocabulary)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "none", "table" );
            c.operatorId = "io:cube_plan";
            c.atomicPublication = "json_result_only";
            c.evidence = "family:io + schema read (io_fabric_operators.h: named-dimension "
                         "chunk plan, windowed materialization only)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily();
            c.operatorId = "io:cube_window";
            c.evidence = "family:io + schema read (io_fabric_operators.cpp: FirstWins "
                         "overlap, atomic writeFileAtomic publication)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "none", "none" );
            c.operatorId = "io:cache_prefetch";
            c.atomicPublication = "json_result_only"; // warms caches, no science product
            c.provenance = "none";
            c.evidence = "family:io + schema read (io_fabric_operators.h)";
            rows.push_back( c );
        }
        {
            // Interchange 11.0 trio (census coverage repair).
            ScientificContract c = ioFamily( "any", "table" );
            c.operatorId = "io:subdatasets";
            c.atomicPublication = "json_result_only"; // bounded inventory result
            c.provenance = "none";
            c.evidence = "family:io + schema read (io_operators.h: redacted "
                         "subdataset inventory)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "any", "any" );
            c.operatorId = "io:metadata_patch";
            c.atomicPublication = "direct_write"; // in-place metadata write-back
            c.evidence = "family:io + schema read (io_operators.h: whitelist "
                         "write-back + read-back verification)";
            rows.push_back( c );
        }
        {
            ScientificContract c = ioFamily( "any", "none" );
            c.operatorId = "io:verify_dataset";
            c.atomicPublication = "json_result_only"; // verdict JSON only
            c.provenance = "none";
            c.evidence = "family:io + schema read (io_operators.h: digest "
                         "recompute integrity gate, fails closed)";
            rows.push_back( c );
        }

        // --- Cartography agents (census 2.0: first-party cartography:) ------
        for ( const char *id : { "cartography:compose", "cartography:validate",
                                 "cartography:preflight", "cartography:repair" } )
        {
            ScientificContract c = cartographyFamily( "json_result_only" );
            c.operatorId = id;
            c.evidence = "family:cartography + schema read (cartography_operators.cpp: "
                         "MapSpec verdict JSON, no raster output)";
            rows.push_back( c );
        }
        {
            ScientificContract c = cartographyFamily( "staged_rename" );
            c.operatorId = "cartography:export";
            c.evidence = "family:cartography + schema read (cartography_operators.cpp: "
                         "writes temp → verifies → sha256 → renames)";
            rows.push_back( c );
        }
        {
            ScientificContract c = cartographyFamily( "staged_rename" );
            c.operatorId = "cartography:produce";
            c.outputDomain = "features"; // rendered map artifact path delivered as "output"
            c.evidence = "family:cartography + schema read (cartography_operators.cpp: "
                         "staged produce, cancelled delivery leaves directory untouched)";
            rows.push_back( c );
        }

        // Assemble the sorted map; duplicate ids are an authoring bug.
        std::map<std::string, ScientificContract> table;
        for ( ScientificContract &row : rows )
        {
            const bool inserted = table.emplace( row.operatorId, row ).second;
            if ( !inserted )
                std::abort(); // authoring bug, must fail loudly in every build
        }
        return table;
    }();
    return table;
}

const ScientificContract *findScientificContract( const std::string &operatorId )
{
    const auto &table = scientificContracts();
    const auto it = table.find( operatorId );
    return it == table.end() ? nullptr : &it->second;
}

std::vector<std::string> validateScientificContract( const ScientificContract &contract )
{
    std::vector<std::string> issues;
    auto check = [ & ]( const std::string &field, const std::string &value,
                        const std::vector<std::string> &vocabulary ) {
        if ( !inVocabulary( value, vocabulary ) )
            issues.push_back( contract.operatorId + ": " + field + " value '" + value
                              + "' is outside the closed vocabulary" );
    };
    // Census 2.0: every FIRST-PARTY registration prefix may carry a record.
    // otb:/opencv: are deliberate exemptions (see
    // data/contracts/contract_exemptions.json) and must NOT appear here.
    static const char *kFirstPartyPrefixes[] = { "rs:", "gdal:", "io:", "cartography:" };
    const bool firstParty = std::any_of( std::begin( kFirstPartyPrefixes ),
                                         std::end( kFirstPartyPrefixes ),
                                         [ & ]( const char *prefix ) {
                                             return contract.operatorId.rfind( prefix, 0 ) == 0;
                                         } );
    if ( !firstParty )
        issues.push_back( contract.operatorId
                          + ": operator id must carry a first-party prefix "
                            "(rs:/gdal:/io:/cartography:)" );
    check( "inputDomain", contract.inputDomain, kNumericDomains );
    check( "outputDomain", contract.outputDomain, kNumericDomains );
    check( "scaleOffset", contract.scaleOffset, kScaleOffsetPolicies );
    check( "noDataPolicy", contract.noDataPolicy, kNoDataPolicies );
    check( "categoricalEncoding", contract.categoricalEncoding, kCategoricalEncodings );
    check( "timeAlignment", contract.timeAlignment, kTimeAlignments );
    check( "wavelengthPolicy", contract.wavelengthPolicy, kWavelengthPolicies );
    check( "seedPolicy", contract.seedPolicy, kSeedPolicies );
    check( "cancellationGranularity", contract.cancellationGranularity,
           kCancellationGranularities );
    check( "atomicPublication", contract.atomicPublication, kAtomicPublications );
    check( "provenance", contract.provenance, kProvenanceExpectations );
    if ( contract.evidence.empty() )
        issues.push_back( contract.operatorId + ": evidence anchor is required" );
    if ( !contract.classIdRange.empty() )
    {
        const std::string &r = contract.classIdRange;
        const auto dots = r.find( ".." );
        const bool wellFormed = dots != std::string::npos && dots > 0 && dots + 2 < r.size()
                                && std::all_of( r.begin(), r.begin() + static_cast<long>( dots ),
                                                []( unsigned char ch ) { return std::isdigit( ch ); } )
                                && std::all_of( r.begin() + static_cast<long>( dots ) + 2, r.end(),
                                                []( unsigned char ch ) { return std::isdigit( ch ); } );
        if ( !wellFormed )
            issues.push_back( contract.operatorId + ": classIdRange '" + r
                              + "' must look like '<low>..<high>'" );
    }
    return issues;
}

Json::Value scientificContractToJson( const ScientificContract &contract )
{
    Json::Value json( Json::objectValue );
    json["operator_id"] = contract.operatorId;
    json["input_domain"] = contract.inputDomain;
    json["output_domain"] = contract.outputDomain;
    json["scale_offset"] = contract.scaleOffset;
    json["no_data_policy"] = contract.noDataPolicy;
    json["categorical_encoding"] = contract.categoricalEncoding;
    json["class_id_range"] = contract.classIdRange;
    json["time_alignment"] = contract.timeAlignment;
    json["wavelength_policy"] = contract.wavelengthPolicy;
    json["seed_policy"] = contract.seedPolicy;
    json["cancellation_granularity"] = contract.cancellationGranularity;
    json["atomic_publication"] = contract.atomicPublication;
    json["provenance"] = contract.provenance;
    Json::Value codes( Json::arrayValue );
    for ( const std::string &code : contract.refusalCodes )
        codes.append( code );
    json["refusal_codes"] = codes;
    json["evidence"] = contract.evidence;
    json["note"] = contract.note;
    return json;
}

bool scientificContractFromJson( const Json::Value &json, ScientificContract &out,
                                 std::string &error )
{
    if ( !json.isObject() || json["operator_id"].empty() )
    {
        error = "not an exp.scientific_contract.v1 object";
        return false;
    }
    out.operatorId = json["operator_id"].asString();
    out.inputDomain = json.get( "input_domain", "any" ).asString();
    out.outputDomain = json.get( "output_domain", "features" ).asString();
    out.scaleOffset = json.get( "scale_offset", "identity" ).asString();
    out.noDataPolicy = json.get( "no_data_policy", "propagate" ).asString();
    out.categoricalEncoding = json.get( "categorical_encoding", "none" ).asString();
    out.classIdRange = json.get( "class_id_range", "" ).asString();
    out.timeAlignment = json.get( "time_alignment", "single_scene" ).asString();
    out.wavelengthPolicy = json.get( "wavelength_policy", "not_applicable" ).asString();
    out.seedPolicy = json.get( "seed_policy", "none" ).asString();
    out.cancellationGranularity =
        json.get( "cancellation_granularity", "operator_level" ).asString();
    out.atomicPublication = json.get( "atomic_publication", "no_partial_output" ).asString();
    out.provenance = json.get( "provenance", "none" ).asString();
    out.refusalCodes.clear();
    for ( const Json::Value &code : json["refusal_codes"] )
        out.refusalCodes.push_back( code.asString() );
    out.evidence = json.get( "evidence", "" ).asString();
    out.note = json.get( "note", "" ).asString();
    if ( !validateScientificContract( out ).empty() )
    {
        error = out.operatorId + ": vocabulary violation in parsed record";
        return false;
    }
    return true;
}

Json::Value scientificContractsToJson()
{
    Json::Value root( Json::objectValue );
    root["schema"] = "exp.scientific_contract.v1";
    Json::Value array( Json::arrayValue );
    for ( const auto &[id, contract] : scientificContracts() )
    {
        ( void )id;
        array.append( scientificContractToJson( contract ) );
    }
    root["contracts"] = array;
    return root;
}

} // namespace sicnu::contracts
