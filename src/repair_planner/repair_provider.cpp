// src/repair_planner/repair_provider.cpp
#include "repair_provider.h"

#include "repair_requirement.h"

#include <set>

namespace sicnu::repair {

namespace {

/// The closed candidate routing: requirement kind -> the operator ids that
/// can serve it. Every id is an EXISTING capability-knowledge entry (loaded
/// from data/agent/capabilities/*.json — the integration test pins this
/// table against those documents). The planner composes capabilities; it
/// never invents one, so an id the knowledge layer does not ship simply
/// routes nothing.
const std::map<std::string, std::vector<std::string>> &requirementOperatorTable()
{
    static const std::map<std::string, std::vector<std::string>> kTable = {
        { requirement_kind::kCrsAlign,
          { "rs:resample", "rs:align", "gdal:reproject", "io:warp",
            "gdal:orthorectification", "rs:modis_georeference" } },
        { requirement_kind::kGridAlign,
          { "rs:resample", "rs:align", "gdal:reproject", "io:warp",
            "gdal:orthorectification", "rs:modis_georeference" } },
        { requirement_kind::kRadiometricState,
          { "rs:radiometric_calibration", "rs:dn_to_radiance",
            "rs:atmospheric_correction", "rs:atmospheric_dos1",
            "rs:atmospheric_dos2", "rs:atmospheric_quac",
            "rs:brdf_normalization" } },
        { requirement_kind::kCalibrationDomain,
          { "rs:radiometric_calibration", "rs:dn_to_radiance",
            "rs:sar_calibrate" } },
        { requirement_kind::kQualityMask,
          { "rs:qa_mask", "rs:apply_mask", "rs:quality_mosaic",
            "rs:sar_terrain_masks" } },
        { requirement_kind::kBandRole, { "rs:extract_bands" } },
        { requirement_kind::kPolarizationSelect,
          { "rs:extract_bands", "rs:sar_backscatter" } },
        { requirement_kind::kTemporalAlign,
          { "rs:temporal_regularize", "rs:temporal_gap_fill",
            "rs:temporal_composite" } },
        { requirement_kind::kModelContract,
          { "rs:feature_stack", "rs:feature_normalize", "rs:feature_select" } },
        { requirement_kind::kDatasetSubstitution, { "io:catalog_search" } },
        // Decision-only kinds (modality_check, training_data,
        // categorical_check) route nothing: the surfaced decision IS the
        // plan, synthesized by the planner from its closed contract.
    };
    return kTable;
}

bool hasUsableId( const Json::Value &entry )
{
    return entry.isObject() && entry.isMember( "id" ) && entry["id"].isString() &&
           !entry["id"].asString().empty();
}

void appendEntry( std::map<std::string, Json::Value> &families,
                  const std::string &kind, const Json::Value &entry )
{
    if ( !families.count( kind ) )
        families[kind] = Json::Value( Json::arrayValue );
    families[kind].append( entry );
}

} // namespace

std::vector<std::string> servingOperatorsForRequirement( const std::string &requirementKind )
{
    const auto it = requirementOperatorTable().find( requirementKind );
    return it == requirementOperatorTable().end() ? std::vector<std::string>{}
                                                : it->second;
}

bool JsonRepairCapabilityProvider::build( const std::map<std::string, Json::Value> &families,
                                          JsonRepairCapabilityProvider &out, RepairError &error )
{
    for ( const auto &[kind, entries] : families )
    {
        if ( !entries.isArray() )
        {
            error = RepairError{ "invalid_input",
                                 "capability family must map to an array: " + kind };
            return false;
        }
        for ( const Json::Value &entry : entries )
        {
            if ( !hasUsableId( entry ) )
            {
                error = RepairError{ "invalid_input",
                                     "capability entry must carry a non-empty string id" };
                return false;
            }
        }
    }
    out.mFamilies = families;
    return true;
}

bool JsonRepairCapabilityProvider::buildFromCapabilityEntries(
    const std::vector<Json::Value> &entries, JsonRepairCapabilityProvider &out,
    RepairError &error )
{
    // Index the documents by id, then route each requirement kind over its
    // closed operator-id set. Entries outside the table serve no repair
    // requirement and are skipped; family-default documents ("family:*") are
    // knowledge, not operators, and never route.
    std::map<std::string, Json::Value> byId;
    for ( const Json::Value &entry : entries )
    {
        if ( !hasUsableId( entry ) )
        {
            error = RepairError{ "invalid_input",
                                 "capability entry must carry a non-empty string id" };
            return false;
        }
        const std::string id = entry["id"].asString();
        if ( id.rfind( "family:", 0 ) == 0 )
            continue;
        if ( !byId.count( id ) )
            byId[id] = entry;
    }

    std::map<std::string, Json::Value> routed;
    for ( const auto &[kind, operatorIds] : requirementOperatorTable() )
    {
        for ( const std::string &id : operatorIds )
        {
            const auto it = byId.find( id );
            if ( it != byId.end() )
                appendEntry( routed, kind, it->second );
        }
    }
    out.mFamilies = routed;
    return true;
}

std::vector<Json::Value> JsonRepairCapabilityProvider::capabilitiesForRequirement(
    const std::string &requirementKind ) const
{
    const auto it = mFamilies.find( requirementKind );
    if ( it == mFamilies.end() )
        return {};
    std::vector<Json::Value> out;
    for ( const Json::Value &entry : it->second )
        out.push_back( entry );
    return out;
}

bool JsonRepairCapabilityProvider::knowsRequirementKind(
    const std::string &requirementKind ) const
{
    return mFamilies.count( requirementKind ) > 0;
}

} // namespace sicnu::repair
