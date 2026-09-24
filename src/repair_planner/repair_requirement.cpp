// src/repair_planner/repair_requirement.cpp
#include "repair_requirement.h"

#include <algorithm>
#include <map>
#include <set>

namespace sicnu::repair {

namespace {

/// The closed finding-code -> requirement-kind table. Codes are the ones the
/// codebase already emits (scientific_preflight.cpp rule packs over
/// harness_error.h error_codes::*); nothing here is invented.
const std::map<std::string, std::string> &findingCodeTable()
{
    static const std::map<std::string, std::string> kTable = {
        { "CRS_MISMATCH", requirement_kind::kCrsAlign },
        { "GRID_MISMATCH", requirement_kind::kGridAlign },
        { "INVALID_RADIOMETRY", requirement_kind::kRadiometricState },
        { "CALIBRATION_MISMATCH", requirement_kind::kCalibrationDomain },
        { "POLARIZATION_MISMATCH", requirement_kind::kPolarizationSelect },
        { "MODALITY_MISMATCH", requirement_kind::kModalityCheck },
        { "BAND_ROLE_UNRESOLVED", requirement_kind::kBandRole },
        { "WAVELENGTH_INCOMPATIBLE", requirement_kind::kBandRole },
        { "BAND_IDENTITY_MISMATCH", requirement_kind::kBandRole },
        { "TIME_ORDER_INVALID", requirement_kind::kTemporalAlign },
        { "TEMPORAL_MISALIGNMENT", requirement_kind::kTemporalAlign },
        { "TEMPORAL_CALENDAR_CONFLICT", requirement_kind::kTemporalAlign },
        { "ACQUISITION_DATES_MISSING", requirement_kind::kTemporalAlign },
        { "DATES_NOT_ASCENDING", requirement_kind::kTemporalAlign },
        { "MODEL_INCOMPATIBLE", requirement_kind::kModelContract },
        { "MODEL_NOT_READY", requirement_kind::kModelContract },
        { "TRAINING_INVALID", requirement_kind::kTrainingData },
        { "DATASET_NOT_FOUND", requirement_kind::kDatasetSubstitution },
        { "CATEGORICAL_MISMATCH", requirement_kind::kCategoricalCheck },
    };
    return kTable;
}

/// Severity rank: blocking findings sort first. Accepts both the harness
/// preflight ladder (error/warning/advice/info) and the preflight-engine
/// ladder (block/require_ack/warn/info) so either producer's documents map
/// without renaming.
int severityRank( const std::string &severity )
{
    static const std::map<std::string, int> kRanks = {
        { "block", 3 },   { "error", 3 }, { "require_ack", 2 },
        { "warning", 1 }, { "warn", 1 },  { "advice", 0 },
        { "info", 0 },
    };
    const auto it = kRanks.find( severity );
    return it == kRanks.end() ? -1 : it->second;
}

} // namespace

bool isKnownRequirementKind( const std::string &kind )
{
    static const std::set<std::string> kSet = {
        requirement_kind::kCrsAlign,
        requirement_kind::kGridAlign,
        requirement_kind::kRadiometricState,
        requirement_kind::kCalibrationDomain,
        requirement_kind::kPolarizationSelect,
        requirement_kind::kModalityCheck,
        requirement_kind::kBandRole,
        requirement_kind::kTemporalAlign,
        requirement_kind::kModelContract,
        requirement_kind::kTrainingData,
        requirement_kind::kDatasetSubstitution,
        requirement_kind::kCategoricalCheck,
        requirement_kind::kQualityMask,
        requirement_kind::kUnsupported,
    };
    return kSet.count( kind ) > 0;
}

std::string requirementKindForFindingCode( const std::string &findingCode )
{
    const auto it = findingCodeTable().find( findingCode );
    return it == findingCodeTable().end() ? std::string() : it->second;
}

std::string normalizeFindingSeverity( const std::string &severity )
{
    static const std::map<std::string, std::string> kUnified = {
        { "block", "error" },         { "error", "error" },
        { "require_ack", "warning" }, { "warning", "warning" },
        { "warn", "warning" },        { "advice", "advice" },
        { "info", "info" },
    };
    const auto it = kUnified.find( severity );
    return it == kUnified.end() ? std::string() : it->second;
}

bool synthesizeRequirements( const std::vector<Json::Value> &findings,
                             std::vector<RepairRequirement> &out, RepairError &error )
{
    out.clear();
    for ( const Json::Value &finding : findings )
    {
        if ( !finding.isObject() )
        {
            error = RepairError{ "invalid_input", "finding must be a JSON object" };
            out.clear();
            return false;
        }
        if ( !finding.isMember( "code" ) || !finding["code"].isString() ||
             finding["code"].asString().empty() )
        {
            error = RepairError{ "invalid_input", "finding must carry a non-empty code" };
            out.clear();
            return false;
        }
        const std::string rawSeverity =
            finding.isMember( "severity" ) && finding["severity"].isString()
                ? finding["severity"].asString()
                : std::string();
        const std::string severity = normalizeFindingSeverity( rawSeverity );
        if ( severity.empty() )
        {
            error = RepairError{ "invalid_input",
                                 "unknown finding severity: " + rawSeverity };
            out.clear();
            return false;
        }

        RepairRequirement requirement;
        requirement.findingCode = finding["code"].asString();
        requirement.severity = severity;
        requirement.blocking = severityRank( severity ) >= 3;
        requirement.subject =
            finding.isMember( "subject" ) && finding["subject"].isString()
                ? finding["subject"].asString()
                : std::string();
        if ( finding.isMember( "evidence" ) )
        {
            if ( !finding["evidence"].isObject() )
            {
                error = RepairError{ "invalid_input",
                                     "finding evidence must be a JSON object" };
                out.clear();
                return false;
            }
            requirement.evidence = finding["evidence"];
        }

        requirement.kind = requirementKindForFindingCode( requirement.findingCode );
        if ( requirement.kind.empty() )
        {
            requirement.kind = requirement_kind::kUnsupported;
            requirement.unsupportedReason =
                "finding code outside the closed requirement mapping: " +
                requirement.findingCode;
        }
        out.push_back( requirement );
    }

    std::sort( out.begin(), out.end(), []( const RepairRequirement &a,
                                           const RepairRequirement &b ) {
        if ( severityRank( a.severity ) != severityRank( b.severity ) )
            return severityRank( a.severity ) > severityRank( b.severity );
        if ( a.findingCode != b.findingCode )
            return a.findingCode < b.findingCode;
        if ( a.subject != b.subject )
            return a.subject < b.subject;
        // Final tie-break keeps the order total for findings equal on
        // (severity, code, subject) but differing in evidence payloads.
        // Canonical compact serialization is deterministic (jsoncpp sorts
        // object keys); at planner scale (<= maxRequirements entries) the
        // per-comparison serialization cost is bounded and intentional.
        return jsonToString( a.evidence ) < jsonToString( b.evidence );
    } );

    for ( std::size_t i = 0; i < out.size(); ++i )
        out[i].requirementId = "req-" + std::to_string( i + 1 );
    return true;
}

Json::Value requirementToJson( const RepairRequirement &requirement )
{
    Json::Value doc( Json::objectValue );
    doc["requirement_id"] = requirement.requirementId;
    doc["finding_code"] = requirement.findingCode;
    doc["kind"] = requirement.kind;
    doc["severity"] = requirement.severity;
    doc["blocking"] = requirement.blocking;
    doc["subject"] = requirement.subject;
    doc["evidence"] = requirement.evidence;
    if ( !requirement.unsupportedReason.empty() )
        doc["unsupported_reason"] = requirement.unsupportedReason;
    return doc;
}

} // namespace sicnu::repair
