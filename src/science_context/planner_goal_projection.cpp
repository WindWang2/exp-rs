// src/science_context/planner_goal_projection.cpp
#include "science_context/planner_goal_projection.h"

#include "contracts/scientific_contract.h"
#include "planner/planner_vocab.h"
#include "planner/sha256_util.h"

#include <algorithm>
#include <set>

namespace sicnu::science_context {

namespace {

ProjectionIssue issue( const std::string &code, const std::string &detail )
{
    return ProjectionIssue{ code, detail };
}

void sortIssues( std::vector<ProjectionIssue> &issues )
{
    std::sort( issues.begin(), issues.end(),
               []( const ProjectionIssue &a, const ProjectionIssue &b ) {
                   if ( a.code != b.code )
                       return a.code < b.code;
                   return a.detail < b.detail;
               } );
}

Json::Value sectionSourceToJson( const SectionSource &source )
{
    Json::Value json( Json::objectValue );
    json["source"] = contentSourceToString( source.source );
    json["authority"] = source.authority;
    json["revision"] = static_cast<Json::UInt64>( source.revision );
    json["degraded"] = source.degraded;
    return json;
}

/// Autonomy level (L0..L5 wire spelling) → planner mode policy. The bundle's
/// constraint block is the authority for how much the machine may decide.
/// Conservative for every input (unknown levels plan with maximal
/// escalation), but unknown levels are ALSO named as issues — silent
/// conservative mapping would still be default-filling.
sicnu::planner::ModePolicy modeForAutonomyLevel( const std::string &level, bool &known )
{
    sicnu::planner::ModePolicy mode;
    mode.kind = "agent";
    mode.autonomy = "minimal"; // L0-L2 default: every decision escalates
    known = true;
    if ( level == "L5" )
        mode.autonomy = "full";
    else if ( level == "L3" || level == "L4" )
        mode.autonomy = "guided";
    else if ( level == "L0" || level == "L1" || level == "L2" )
        mode.autonomy = "minimal";
    else
        known = false;
    return mode;
}

} // namespace

std::map<std::string, std::string> intentToGoalKindMap()
{
    // Deliberately UNMAPPED live intents (loud unresolved_intent, never a
    // guessed kind): "sar" (change vs water vs ship — the caller must
    // disambiguate), "terrain" (no planner goal kind owns terrain products),
    // "zonal" (statistics over zones — no analysis slot projects it yet),
    // "accuracy"/"qa"/"preprocess" (not science goals). Drift-pinned against
    // the harness intent vocabulary by tests/test_science_context_goal_projection.
    static const std::map<std::string, std::string> kMap = {
        // spectral index measurement (the planner's measurement kind)
        { "ndvi", "measurement" },   { "evi", "measurement" },
        { "savi", "measurement" },   { "ndre", "measurement" },
        { "ndwi", "measurement" },   { "mndwi", "measurement" },
        { "ndsi", "measurement" },   { "ndbi", "measurement" },
        { "bsi", "measurement" },
        // change
        { "change", "change" },      { "nbr", "change" },
        { "dnbr", "change" },        { "sar_change", "change" },
        // classification
        { "classify", "classification" },
        // temporal
        { "temporal", "temporal_analysis" }, { "phenology", "temporal_analysis" },
        // target detection / mapping
        { "water", "detection" },    { "flood", "detection" },
        { "sar_water", "detection" }, { "sar_flood", "detection" },
        { "ship", "detection" },     { "inference", "detection" },
    };
    return kMap;
}

std::map<std::string, std::string> radiometricUnitToDomainMap()
{
    // Keys must stay members of scientific_state's normalized radiometric
    // vocabulary (normalizeRadiometricToken, asset_state_resolver.h) and
    // values must be contracts numeric domains — both pinned by
    // test_science_context_goal_projection. A unit added to the passport
    // authority without a map entry degrades LOUDLY (foreign_radiometric_unit
    // issue + undeclared domain), so the drift is contained, never silent.
    static const std::map<std::string, std::string> kMap = {
        { "digital_number", "dn" },
        { "radiance", "radiance" },
        { "toa_reflectance", "reflectance" },
        { "surface_reflectance", "reflectance" },
        { "brightness_temperature", "temperature" },
        { "sigma0", "sigma0" },
        { "gamma0", "gamma0" },
        { "beta0", "beta0" },
    };
    return kMap;
}

PlannerProjectionResult projectPlannerInputs( const ScientificContextBundle &bundle,
                                              const PlannerAssetEnrichment &enrichment )
{
    PlannerProjectionResult result;
    std::vector<ProjectionIssue> issues;

    // ---- goal: bundle intent → planner goal kind (never default-filled) --
    sicnu::planner::ScientificGoal &goal = result.goal;
    const auto &intentMap = intentToGoalKindMap();
    const auto intentIt = intentMap.find( bundle.intent );
    if ( intentIt != intentMap.end() )
    {
        goal.kind = intentIt->second;
    }
    else
    {
        issues.push_back( issue(
            projection_issue::kUnresolvedIntent,
            "bundle intent \"" + bundle.intent
                + "\" has no planner goal kind — resolve the goal before planning" ) );
    }
    goal.subject = bundle.goal;

    // Deterministic identity from the projected content. The digest covers
    // bundleId, the projected intent/goal AND the projected asset id set, so
    // bundles that differ in their assets project different goal ids even
    // when a caller reuses a bundleId (callers are expected to carry the
    // broker's computeBundleId, but the projection does not trust that).
    std::vector<std::string> assetIdMaterial;
    assetIdMaterial.reserve( bundle.assets.size() );
    for ( const auto &asset : bundle.assets )
        assetIdMaterial.push_back( asset.assetId );
    std::sort( assetIdMaterial.begin(), assetIdMaterial.end() );
    std::string assetMaterial;
    for ( const auto &id : assetIdMaterial )
        assetMaterial += id + ";";
    goal.goalId = "goal-" + sicnu::planner::fingerprint16( bundle.bundleId + "|" + bundle.intent
                                                           + "|" + bundle.goal + "|"
                                                           + assetMaterial );

    // ---- assets ----------------------------------------------------------
    std::set<std::string> bundleAssetIds;
    for ( const auto &asset : bundle.assets )
    {
        if ( asset.assetId.empty() )
        {
            issues.push_back( issue( projection_issue::kEmptyAssetId,
                                     "bundle carries an asset without an id — skipped" ) );
            continue;
        }
        // The planner bounds asset refs at PlanLimits::kMaxIdChars; a longer
        // id cannot survive the plan's own fail-closed schema, so it is
        // named and skipped instead of flowing into a plan that cannot
        // round-trip.
        if ( asset.assetId.size() > sicnu::planner::PlanLimits::kMaxIdChars )
        {
            issues.push_back( issue( projection_issue::kOversizedAssetId,
                                     "asset id \"" + asset.assetId + "\" exceeds the "
                                         + std::to_string(
                                               sicnu::planner::PlanLimits::kMaxIdChars )
                                         + "-char planner bound — skipped" ) );
            continue;
        }
        if ( !bundleAssetIds.insert( asset.assetId ).second )
        {
            issues.push_back( issue( projection_issue::kDuplicateAssetId,
                                     "asset id \"" + asset.assetId
                                         + "\" is declared more than once — projected once" ) );
            continue;
        }

        sicnu::planner::PlannerAssetFacts facts;
        facts.ref = asset.assetId;
        facts.kind = "raster"; // bundle assets are RS raster surfaces by schema
        // Modality projection onto the planner's closed axis. Loud-unknown
        // beats silent projection: hyperspectral is optical-family imaging
        // (mapped), but thermal has no honest planner home (kAssetModalities
        // carries no thermal) — it degrades to "unknown" WITH an issue
        // instead of quietly becoming optical.
        const std::string modality =
            ( asset.modality == "optical" || asset.modality == "hyperspectral" )
                ? "optical"
                : ( asset.modality == "sar" ? "sar"
                                            : ( asset.modality == "dem" ? "dem" : "unknown" ) );
        if ( modality == "unknown" && !asset.modality.empty() && asset.modality != "unknown" )
        {
            issues.push_back( issue( projection_issue::kUnmappedModality,
                                     "asset \"" + asset.assetId + "\" modality \""
                                         + asset.modality + "\" has no planner modality" ) );
        }
        facts.modality = modality;

        const auto &unitMap = radiometricUnitToDomainMap();
        const auto unitIt = unitMap.find( asset.radiometricUnit );
        if ( unitIt != unitMap.end() )
        {
            facts.numericDomain = unitIt->second;
        }
        else
        {
            // Empty = undeclared (the planner's lawful unknown form): the
            // asset cannot claim a contracts domain it does not have. If the
            // unit was a non-empty foreign token, the degradation is LOUD.
            if ( !asset.radiometricUnit.empty() )
            {
                issues.push_back( issue( projection_issue::kForeignRadiometricUnit,
                                         "asset \"" + asset.assetId + "\" radiometric unit \""
                                             + asset.radiometricUnit
                                             + "\" has no contracts numeric domain" ) );
            }
        }

        facts.crs = asset.crsAuthid;
        facts.resolutionM = -1; // the bundle does not declare resolution
        facts.bandRoles = asset.bandRoles;
        // The bundle drops lifecycle state by design: without enrichment the
        // asset cannot back a hard fact, and the planner will say so.
        facts.state = "unknown";
        if ( !asset.conflictAlternatives.empty() )
        {
            issues.push_back( issue( projection_issue::kConflictedEvidence,
                                     "asset \"" + asset.assetId + "\" carries "
                                         + std::to_string( asset.conflictAlternatives.size() )
                                         + " conflicting claim alternative(s)" ) );
        }

        // Caller-selected passport facts, vocabulary-validated per field.
        const auto enrichmentIt = enrichment.assets.find( asset.assetId );
        if ( enrichmentIt != enrichment.assets.end() )
        {
            const sicnu::planner::PlannerAssetFacts &extra = enrichmentIt->second;
            const auto reject = [&]( const char *field ) {
                issues.push_back( issue( projection_issue::kEnrichmentRejected,
                                         std::string( "asset \"" ) + asset.assetId
                                             + "\" enrichment " + field
                                             + " is outside the planner vocabulary" ) );
            };
            if ( !extra.kind.empty() )
            {
                if ( sicnu::planner::isKnownAssetKind( extra.kind ) )
                    facts.kind = extra.kind;
                else
                    reject( "kind" );
            }
            if ( !extra.state.empty() )
            {
                if ( sicnu::planner::isKnownAssetState( extra.state ) )
                    facts.state = extra.state;
                else
                    reject( "state" );
            }
            if ( !extra.numericDomain.empty() )
            {
                if ( sicnu::planner::isKnownContractsNumericDomain( extra.numericDomain ) )
                    facts.numericDomain = extra.numericDomain;
                else
                    reject( "numeric_domain" );
            }
            if ( !extra.modality.empty() )
            {
                if ( sicnu::planner::isKnownVocabValue( sicnu::planner::kAssetModalities,
                                                        extra.modality ) )
                    facts.modality = extra.modality;
                else
                    reject( "modality" );
            }
            if ( extra.resolutionM >= 0 )
                facts.resolutionM = extra.resolutionM;
            if ( !extra.crs.empty() )
                facts.crs = extra.crs;
            if ( !extra.dates.empty() )
                facts.dates = extra.dates;
            if ( extra.qualityMaskAvailable )
                facts.qualityMaskAvailable = true;
            if ( !extra.calibrationState.empty() )
                facts.calibrationState = extra.calibrationState;
        }

        result.context.assets.push_back( facts );
    }
    for ( const auto &[assetId, facts] : enrichment.assets )
    {
        (void)facts;
        if ( !bundleAssetIds.count( assetId ) )
        {
            issues.push_back( issue( projection_issue::kEnrichmentRejected,
                                     "enrichment names \"" + assetId
                                         + "\" which the bundle does not carry" ) );
        }
    }

    // ---- constraints / quality / mode -------------------------------------
    result.context.constraints.requiredDeterminism = bundle.constraints.determinismRequired;
    bool autonomyKnown = false;
    result.context.mode = modeForAutonomyLevel( bundle.constraints.autonomyLevel, autonomyKnown );
    if ( !autonomyKnown )
    {
        issues.push_back( issue( projection_issue::kUnmappedAutonomyLevel,
                                 "autonomy level \"" + bundle.constraints.autonomyLevel
                                     + "\" is outside L0..L5 — planning conservatively with "
                                       "minimal autonomy" ) );
    }

    // ---- provenance --------------------------------------------------------
    Json::Value provenance( Json::objectValue );
    provenance["bundle_id"] = bundle.bundleId;
    provenance["schema"] = bundle.schemaId;
    Json::Value sections( Json::objectValue );
    sections["assets"] = sectionSourceToJson( bundle.sources.assets );
    sections["capabilities"] = sectionSourceToJson( bundle.sources.capabilities );
    sections["recipes"] = sectionSourceToJson( bundle.sources.recipes );
    sections["planner_facts"] = sectionSourceToJson( bundle.sources.plannerFacts );
    provenance["sections"] = sections;
    if ( !enrichment.source.empty() || enrichment.revision != 0 )
    {
        Json::Value enrichmentJson( Json::objectValue );
        enrichmentJson["source"] = enrichment.source;
        enrichmentJson["revision"] = static_cast<Json::UInt64>( enrichment.revision );
        provenance["enrichment"] = enrichmentJson;
    }

    sortIssues( issues );
    result.unresolved = std::move( issues );
    provenance["unresolved_count"] = static_cast<Json::UInt64>( result.unresolved.size() );
    result.provenance = provenance;
    return result;
}

} // namespace sicnu::science_context
