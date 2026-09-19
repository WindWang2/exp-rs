// tests/test_capability_completeness.cpp
//
// Track D1 (ds41-capability-help-sync) help/capability completeness gate.
//
// ADR 0154 shipped the v2 capability sidecars with a shape-and-vocabulary
// validator (CapabilityCatalog::validateEntry) but no completeness contract:
// `summary`, `failure_modes`, and the `io` arrays may all legally be empty, so
// a first-class capability could ship with no purpose, no failure guidance and
// no input/output contract while every existing gate stayed green. Measured at
// the Track D1 baseline: 3/119 sidecars had an empty summary, 4/119 empty
// failure_modes, 23/119 empty io.inputs.
//
// This gate enforces, for every first-class rs: capability (no exemptions
// beyond the enumerated io.inputs list below):
//   * purpose          — non-empty `summary`;
//   * failure guidance  — non-empty `failure_modes` (typed, closed-vocabulary
//                         codes are pinned by the validator/guard test);
//   * output contract   — non-empty `io.outputs`;
//   * input contract    — non-empty `io.inputs` OR an enumerated exemption.
//
// The exemption list is compared for EQUALITY against the computed empty set,
// so a new collection-style operator fails until its exemption is added here
// deliberately, and a closed gap cannot silently shrink the list.
//
// Units / NoData: `rs_schema.h`'s make*Param helpers carry no unit field and
// no operator declares a structured NoData contract, so there is nothing to
// project yet. This test reports that coverage as a WARN census instead of
// inventing units (Track D1 non-goal: no fabricated scientific text). The
// infrastructure gap is tracked as a known limitation in the PR body.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/capability_catalog.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <set>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#error "CMAKE_SOURCE_DIR must point at the repo source tree"
#endif

using namespace sicnu;

namespace {

struct Bootstrap
{
    agent::harness::CapabilityCatalog &catalog;
    std::vector<std::string> ids;

    explicit Bootstrap()
      : catalog( agent::harness::CapabilityCatalog::instance() )
    {
        operators::RSOperatorRegistry::instance();
        operators::rs::initBuiltinRsOperators();
        operators::rs::installRsOperatorProvider();
        catalog.setDirectory( std::string( CMAKE_SOURCE_DIR ) +
                              "/data/processing/algorithm_meta/capability" );
        catalog.reload();
        REQUIRE( catalog.loadProblems().empty() );
        ids = catalog.entryIds();
        std::sort( ids.begin(), ids.end() );
        REQUIRE_FALSE( ids.empty() );
    }
};

/// Collection-style operators whose data inputs are declared as array-of-string
/// path parameters (`scenes` / `inputs`), not as raster data ports — the
/// derivation in deriveCapabilityBlock therefore leaves io.inputs empty by
/// design (verified against the live schemas, e.g. rs:mosaic / rs:temporal_anomaly).
/// Every exempted id states the same reason; the list is closed and compared
/// for equality, so additions require editing this table.
const std::vector<std::pair<std::string, std::string>> kIoInputsExempt = {
    { "rs:cn_product_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:endmember_analysis", "collection-style: inputs are array-of-string path parameters" },
    { "rs:gaofen_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:hj_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:landsat_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:library_select", "collection-style: inputs are array-of-string path parameters" },
    { "rs:modis_georeference", "collection-style: inputs are array-of-string path parameters" },
    { "rs:modis_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:mosaic", "collection-style: inputs are array-of-string path parameters" },
    { "rs:quality_mosaic", "collection-style: inputs are array-of-string path parameters" },
    { "rs:register_images", "collection-style: inputs are array-of-string path parameters" },
    { "rs:sar_network_inversion", "collection-style: inputs are array-of-string path parameters" },
    { "rs:sar_pair_network", "collection-style: inputs are array-of-string path parameters" },
    { "rs:sar_temporal_events", "collection-style: inputs are array-of-string path parameters" },
    { "rs:sar_temporal_stats", "collection-style: inputs are array-of-string path parameters" },
    { "rs:sentinel2_import", "collection-style: inputs are array-of-string path parameters" },
    { "rs:stack_register", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_anomaly", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_breakpoints", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_composite", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_decompose", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_extract_regions", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_extract_series", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_gap_fill", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_harmonic_breaks", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_harmonic_fit", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_index_series", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_model_select", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_monitor", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_phenology", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_phenology_multi", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_region_features", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_regularize", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_seasonal_breaks", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_sen_trend", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_smooth", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_summary", "collection-style: inputs are array-of-string path parameters" },
    { "rs:temporal_trend", "collection-style: inputs are array-of-string path parameters" },
    { "rs:zy3_import", "collection-style: inputs are array-of-string path parameters" },
};

std::set<std::string> exemptIds()
{
    std::set<std::string> out;
    for ( const auto &[ id, reason ] : kIoInputsExempt )
      out.insert( id );
    return out;
}

bool mentionsNoData( const Json::Value &block )
{
    // Census-only heuristic over the authored strings; not a contract.
    static const char *needles[] = { "NoData", "nodata", "no_data", "无效值", "空值" };
    const std::string text = block.toStyledString();
    for ( const char *needle : needles )
      if ( text.find( needle ) != std::string::npos )
        return true;
    return false;
}

} // namespace

TEST_CASE( "First-class capabilities carry purpose, failure guidance and io contracts",
           "[capability][completeness]" )
{
    Bootstrap boot;
    const std::set<std::string> exempt = exemptIds();

    std::set<std::string> emptyIoInputs;
    int noDataMentions = 0;
    for ( const std::string &id : boot.ids )
    {
        const Json::Value block = boot.catalog.capability( id );
        INFO( "operator: " << id );

        const std::string summary = block[ "summary" ].isString() ? block[ "summary" ].asString() : "";
        REQUIRE_FALSE( summary.empty() );

        REQUIRE( block[ "failure_modes" ].isArray() );
        REQUIRE_FALSE( block[ "failure_modes" ].empty() );

        REQUIRE( block[ "io" ].isObject() );
        REQUIRE( block[ "io" ][ "outputs" ].isArray() );
        REQUIRE_FALSE( block[ "io" ][ "outputs" ].empty() );

        const Json::Value &inputs = block[ "io" ][ "inputs" ];
        REQUIRE( inputs.isArray() );
        if ( inputs.empty() )
          emptyIoInputs.insert( id );

        if ( mentionsNoData( block ) )
          ++noDataMentions;
    }

    // The exemption table and reality must agree exactly: a new gap fails
    // until it is exempted here deliberately, and a closed gap must remove
    // its entry.
    for ( const std::string &id : emptyIoInputs )
      if ( !exempt.count( id ) )
        FAIL( "empty io.inputs without an exemption entry: " + id );
    for ( const std::string &id : exempt )
      if ( !emptyIoInputs.count( id ) )
        FAIL( "stale io.inputs exemption (inputs are no longer empty): " + id );

    // Units / NoData census — informational only. rs_schema.h carries no unit
    // field and no structured NoData contract exists to project; inventing
    // either is a Track D1 non-goal, so the gap is measured, not asserted.
    WARN( "NoData semantics mentioned by " << noDataMentions << " / " << boot.ids.size()
                                           << " capability sidecars (no structured contract exists yet)" );
}
