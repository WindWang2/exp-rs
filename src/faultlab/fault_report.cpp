// fault_report.cpp — `sicnu.faultlab.report/1` canonical serialization.
#include "fault_report.h"

#include "util/canonical_json.h"

namespace sicnu::faultlab
{

Json::Value FaultRunReport::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultReportSchemaId;
    doc["scenario_id"] = scenarioId;
    doc["title"] = title;
    doc["fault"] = Json::Value( Json::objectValue );
    doc["fault"]["family"] = faultFamily;
    doc["fault"]["seed"] = seed;
    doc["fault"]["applied"] = faultApplied;
    doc["fault"]["mutations"] = faultMutations;
    doc["base_fixture"] = Json::Value( Json::objectValue );
    doc["base_fixture"]["fixture_id"] = fixtureId;
    doc["base_fixture"]["seed"] = fixtureSeed;

    Json::Value evidence( Json::arrayValue );
    for ( const auto &result : expectationResults )
    {
        evidence.append( result.toJson() );
    }
    doc["observable_evidence"] = evidence;
    doc["expectations_passed"] = expectationsPassed;

    doc["expected_diagnosis"] = Json::Value( Json::objectValue );
    doc["expected_diagnosis"]["signature"] = expectedDiagnosisSignature;
    doc["actual_diagnosis"] = Json::Value( Json::objectValue );
    doc["actual_diagnosis"]["signature"] = actualDiagnosisSignature;
    doc["actual_diagnosis"]["matched"] = diagnosisMatched;

    doc["cleanup"] = Json::Value( Json::objectValue );
    doc["cleanup"]["sandbox_removed"] = sandboxRemoved;
    doc["cleanup"]["source_digest_before"] = sourceDigestBefore;
    doc["cleanup"]["source_digest_after"] = sourceDigestAfter;
    doc["cleanup"]["source_unchanged"] = sourceUnchanged;

    doc["replay"] = Json::Value( Json::objectValue );
    doc["replay"]["digest"] = replayDigest;
    doc["replay"]["deterministic"] = replayDeterministic;

    Json::Value diagnostics( Json::arrayValue );
    for ( const auto &diagnostic : this->diagnostics )
    {
        diagnostics.append( diagnostic.toJson() );
    }
    doc["diagnostics"] = diagnostics;

    doc["verdict"] = Json::Value( Json::objectValue );
    doc["verdict"]["passed"] = passed;
    doc["verdict"]["source_unchanged"] = sourceUnchanged;
    doc["verdict"]["replay_deterministic"] = replayDeterministic;
    return doc;
}

std::string faultReportToJson( const FaultRunReport &report )
{
    return canonical::toCanonicalString( report.toJson() );
}

std::string faultReportDigest( const FaultRunReport &report )
{
    return canonical::sha256HexOf( report.toJson() );
}

} // namespace sicnu::faultlab
