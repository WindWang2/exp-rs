// src/planner/live/capability_live.cpp
#include "planner/live/capability_live.h"

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"
#include "planner/sha256_util.h"
#include "preflight/capability_mirror.h"
#include "preflight/provider.h"

#include <algorithm>
#include <map>
#include <set>

namespace sicnu::planner {

namespace {

/// The explicit harness-mirror-operator → planner-family-slot table.
///
/// Projection policy (docs/integration.md §1, planner slice):
///   * only operators the CONTRACTS registry declares are listed — the
///     planner would exclude anything else as an authority conflict, so
///     projecting it could only manufacture ambiguity questions;
///   * only the family slots the planner core actually queries are declared
///     (planner_core.cpp queries data_import / calibration / alignment /
///     analysis / verification / publication). The mirror declares no
///     verification or publication OPERATOR (its "verification" key carries
///     per-entry artifact checks, not a capability) — those slots stay
///     honestly unprojected, and plans over the live authority report the
///     gap as a typed question instead of a default-filled step;
///   * slots the spine never queries (feature_stack, uncertainty, record)
///     are deliberately absent — declaring them would be dead facts.
///
/// Drift contract: every id here must exist in the live mirror documents
/// AND in the contracts registry (pinned by test_planner_live_capability).
const std::map<std::string, std::vector<const char *>> &slotTable()
{
    static const std::map<std::string, std::vector<const char *>> kTable = {
        { "data_import",
          {
              "rs:cn_product_import", "rs:gaofen_import", "rs:hj_import",
              "rs:landsat_import",    "rs:modis_import",  "rs:sentinel2_import",
              "rs:zy3_import",
          } },
        { "calibration",
          {
              "rs:atmospheric_correction", "rs:atmospheric_dos1",
              "rs:atmospheric_dos2",       "rs:atmospheric_quac",
              "rs:brdf_normalization",     "rs:dn_to_radiance",
              "rs:radiometric_calibration", "rs:sar_backscatter",
              "rs:sar_calibrate",          "rs:topographic_correction",
          } },
        { "alignment",
          {
              "rs:align",              "rs:register_images",
              "rs:resample",           "rs:sar_coregister",
              "rs:sar_coregister_local", "rs:sar_geocode",
              "rs:stack_register",
          } },
        { "analysis",
          {
              // spectral indexes + index math
              "rs:band_ratio",       "rs:band_math",         "rs:continuum_removal",
              "rs:evi",              "rs:mndwi",             "rs:ndbi",
              "rs:ndvi",             "rs:ndwi",              "rs:savi",
              "rs:spectral_derivative", "rs:spectral_index",
              // change
              "rs:change",                  "rs:change_cva",
              "rs:change_cva_angle",        "rs:change_detection",
              "rs:change_difference",       "rs:change_irmad",
              "rs:change_log_ratio",        "rs:change_mad",
              "rs:change_normalized_difference", "rs:change_ratio",
              "rs:change_sam",              "rs:post_classification_change",
              // classification
              "rs:classify",                "rs:kmeans_classification",
              "rs:obia_classify",           "rs:sam_classify",
              "rs:supervised_classification", "rs:threshold_raster",
              // detection / inference
              "rs:cem_detection",           "rs:detect",
              "rs:infer",                   "rs:local_rx_anomaly",
              "rs:matched_filter",          "rs:osp_detection",
              "rs:rx_anomaly",              "rs:tcimf_detection",
              // temporal
              "rs:temporal_anomaly",        "rs:temporal_breakpoints",
              "rs:temporal_composite",      "rs:temporal_decompose",
              "rs:temporal_extract_series", "rs:temporal_gap_fill",
              "rs:temporal_harmonic_breaks", "rs:temporal_harmonic_fit",
              "rs:temporal_index_series",   "rs:temporal_model_select",
              "rs:temporal_monitor",        "rs:temporal_phenology",
              "rs:temporal_phenology_multi", "rs:temporal_region_features",
              "rs:temporal_regularize",     "rs:temporal_sar_fusion",
              "rs:temporal_seasonal_breaks", "rs:temporal_sen_trend",
              "rs:temporal_smooth",         "rs:temporal_summary",
              "rs:temporal_trend",
          } },
    };
    return kTable;
}

/// Stochasticity projection: seed_param = the caller's seed changes the
/// output; the other closed policies are deterministic kernels.
bool deterministicForSeedPolicy( const std::string &seedPolicy )
{
    return seedPolicy != "seed_param";
}

} // namespace

std::string plannerCostClassForMirrorCostClass( const std::string &mirrorCostClass )
{
    static const std::map<std::string, std::string> kMap = {
        { "light", "low" }, { "medium", "medium" }, { "heavy", "high" },
    };
    const auto it = kMap.find( mirrorCostClass );
    return it == kMap.end() ? std::string() : it->second;
}

std::vector<std::string> LiveCapabilityProvider::declaredFamilySlots()
{
    std::vector<std::string> slots;
    for ( const auto &[slot, ids] : slotTable() )
    {
        (void)ids;
        slots.push_back( slot );
    }
    std::sort( slots.begin(), slots.end() );
    return slots;
}

std::vector<std::string> LiveCapabilityProvider::operatorIdsForSlot( const std::string &slot )
{
    std::vector<std::string> ids;
    const auto it = slotTable().find( slot );
    if ( it == slotTable().end() )
        return ids;
    ids.assign( it->second.begin(), it->second.end() );
    std::sort( ids.begin(), ids.end() );
    return ids;
}

bool LiveCapabilityProvider::create( const sicnu::preflight::CapabilityMirrorProjection &mirror,
                                     LiveCapabilityProvider &out, std::string &error )
{
    if ( !mirror.configured() )
    {
        error = "invalid_document: capability mirror is not configured (no documents loaded)";
        return false;
    }
    if ( !mirror.healthy() )
    {
        std::string detail;
        for ( const auto &problem : mirror.problems() )
        {
            if ( !detail.empty() )
                detail += "; ";
            detail += problem;
        }
        error = "invalid_document: capability mirror is unhealthy and must not read as live ("
                + detail + ")";
        return false;
    }

    out = LiveCapabilityProvider{};

    std::set<std::string> declaredIds;
    for ( const auto &[slot, ids] : slotTable() )
    {
        (void)slot;
        declaredIds.insert( ids.begin(), ids.end() );
    }

    // Revision + facts accumulate in one deterministic pass over the sorted
    // mirror id set: the revision digests what was projected, so any base
    // fact change (entry removed, merged content edited) moves it.
    std::string revisionMaterial;
    std::vector<PlannerCapability> facts;
    std::vector<std::string> notProjected;
    std::vector<std::string> skippedNoContract;
    std::vector<std::string> skippedNoCostClass;

    for ( const auto &id : mirror.entryIds() )
    {
        if ( id.rfind( "rs:", 0 ) != 0 )
            continue; // family defaults and tool ids are not plannable operators
        const sicnu::preflight::CapabilityEntryResult entry = mirror.entryForOperator( id, {} );
        if ( entry.status != sicnu::preflight::FactStatus::Available || !entry.entry.isObject() )
            continue; // mirror unhealthy was already failed-closed above
        const std::string canonical = json_util::canonicalCompact( entry.entry );
        revisionMaterial += id + "\n" + canonical + "\n";

        if ( !declaredIds.count( id ) )
        {
            notProjected.push_back( id );
            continue;
        }
        const sicnu::contracts::ScientificContract *contract =
            sicnu::contracts::findScientificContract( id );
        if ( !contract )
        {
            skippedNoContract.push_back( id );
            continue;
        }
        const std::string costClass = plannerCostClassForMirrorCostClass(
            entry.entry["resource"]["cost_class"].asString() );
        if ( costClass.empty() )
        {
            skippedNoCostClass.push_back( id );
            continue;
        }

        // One fact per (slot, id): an operator listed under several slots is
        // several planner facts (the planner queries per family).
        for ( const auto &[slot, ids] : slotTable() )
        {
            if ( std::find( ids.begin(), ids.end(), id ) == ids.end() )
                continue;
            PlannerCapability fact;
            fact.operatorId = id;
            fact.family = slot;
            fact.costClass = costClass;
            fact.estimatedRamMb = 0; // undeclared by the mirror; never fabricated
            fact.deterministic = deterministicForSeedPolicy( contract->seedPolicy );
            fact.inputDomain = contract->inputDomain;
            fact.outputDomain = contract->outputDomain;
            facts.push_back( fact );
        }
    }

    // Stable order: the planner re-ranks, but the provider itself stays
    // byte-deterministic for revision/test purposes.
    std::sort( facts.begin(), facts.end(),
               []( const PlannerCapability &a, const PlannerCapability &b ) {
                   if ( a.family != b.family )
                       return a.family < b.family;
                   return a.operatorId < b.operatorId;
               } );

    std::sort( notProjected.begin(), notProjected.end() );
    std::sort( skippedNoContract.begin(), skippedNoContract.end() );
    std::sort( skippedNoCostClass.begin(), skippedNoCostClass.end() );

    out.mFacts = std::move( facts );
    out.mStats.notProjected = std::move( notProjected );
    out.mStats.skippedNoContract = std::move( skippedNoContract );
    out.mStats.skippedNoCostClass = std::move( skippedNoCostClass );
    out.mStats.projected = static_cast<int>( out.mFacts.size() );
    out.mRevision = fingerprint16( revisionMaterial );
    return true;
}

std::vector<PlannerCapability> LiveCapabilityProvider::capabilitiesForFamily(
    const std::string &family ) const
{
    std::vector<PlannerCapability> result;
    for ( const auto &fact : mFacts )
    {
        if ( fact.family == family )
            result.push_back( fact );
    }
    return result;
}

} // namespace sicnu::planner
