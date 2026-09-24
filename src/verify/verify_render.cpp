// src/verify/verify_render.cpp — deterministic teaching/agent/text renders.
#include "verify_render.h"

#include "verify_error_codes.h"

#include <map>
#include <sstream>
#include <string>

namespace sicnu::verify
{

namespace
{

/// Control characters must not smuggle untrusted ids/messages into the
/// single-line text and teaching surfaces.
std::string safeText( const std::string &text )
{
    std::string clean;
    clean.reserve( text.size() );
    for ( const char raw : text )
    {
        const unsigned char c = static_cast<unsigned char>( raw );
        clean.push_back( c < 0x20 || c == 0x7f ? '?' : raw );
    }
    return clean;
}

} // namespace

std::string suggestedAction( const std::string &code )
{
    // Capability-restoration refinements first: the i_ class names WHICH
    // missing capability to restore.
    static const std::map<std::string, std::string> capabilityActions = {
        { kCodeProviderMissing, "attach_provider_then_reverify" },
        { kCodeArtifactUnreadable, "restore_artifact_access_then_reverify" },
        { kCodeArtifactMissing, "produce_missing_output_then_reverify" },
        { kCodeMetricMissing, "record_metric_then_reverify" },
        { kCodeMetricNotFinite, "recompute_nonfinite_observation_then_reverify" },
        { kCodeProvenanceMissing, "repair_provenance_document_then_reverify" },
        { kCodeDigestUnavailable, "recompute_digest_then_reverify" },
        { kCodeEmptyInput, "supply_nonempty_input_then_reverify" },
    };
    const auto capability = capabilityActions.find( code );
    if ( capability != capabilityActions.end() )
        return capability->second;

    if ( isIndeterminateCode( code ) )
        return "restore_capability_then_reverify";
    if ( isVerifierCode( code ) )
        return "repair_output_then_reverify";
    return "escalate_unverifiable";
}

std::string renderText( const VerificationReport &report )
{
    std::ostringstream text;
    text << "verify(" << safeText( report.specId ) << "): " << statusToString( report.overall )
         << " (fail=" << report.failCount() << " indeterminate=" << report.indeterminateCount()
         << " pass=" << report.passCount() << ")";
    return text.str();
}

std::string renderTeaching( const VerificationReport &report )
{
    std::ostringstream text;
    text << "Verification " << safeText( report.specId ) << " [" << safeText( report.scope )
         << "] — " << statusToString( report.overall ) << "\n";
    for ( const VerificationCheckResult &check : report.checks )
    {
        text << "  [" << statusToString( check.status ) << "] " << safeText( check.checkId ) << " ("
             << safeText( check.kind ) << ")";
        if ( !check.message.empty() )
            text << ": " << safeText( check.message );
        text << "\n";
    }
    text << "Result: " << report.passCount() << " passed, " << report.failCount() << " failed, "
         << report.indeterminateCount() << " could not be judged.";
    return text.str();
}

Json::Value renderAgent( const VerificationReport &report )
{
    Json::Value json( Json::objectValue );
    json["overall"] = statusToWire( report.overall );
    Json::Value counts( Json::objectValue );
    counts["pass"] = static_cast<Json::UInt64>( report.passCount() );
    counts["fail"] = static_cast<Json::UInt64>( report.failCount() );
    counts["indeterminate"] = static_cast<Json::UInt64>( report.indeterminateCount() );
    json["counts"] = counts;

    Json::Value blocking( Json::arrayValue );
    std::string overallAction;
    for ( const VerificationCheckResult &check : report.checks )
    {
        if ( check.status == VerificationStatus::Pass )
            continue;
        Json::Value entry( Json::objectValue );
        entry["checkId"] = check.checkId;
        entry["kind"] = check.kind;
        entry["status"] = statusToWire( check.status );
        entry["code"] = check.code;
        entry["message"] = check.message;
        const std::string action = suggestedAction( check.code );
        entry["suggestedAction"] = action;
        if ( overallAction.empty() )
            overallAction = action;
        blocking.append( entry );
    }
    json["blocking"] = blocking;
    json["suggestedAction"] = overallAction.empty() ? std::string( "none" ) : overallAction;
    return json;
}

} // namespace sicnu::verify
