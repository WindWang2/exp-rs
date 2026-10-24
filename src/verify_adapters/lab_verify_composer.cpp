// src/verify_adapters/lab_verify_composer.cpp — see header for the contract.

#include "lab_verify_composer.h"

#include "fs_artifact_probe.h"
#include "gdal_grid_probe.h"

#include "verify/verify_engine.h"
#include "verify/verify_status.h"
#include "verify/verify_types.h"

namespace sicnu::verify_adapters
{

namespace
{

constexpr const char *kEvidenceSchema = "sicnu.verifier.evidence/1";
constexpr const char *kTeachingSpecId = "teaching.artifact/1";

/// fail-closed indeterminate document for composition-internal refusals: the
/// caller must see "could not verify", never an absent block, never a pass.
/// ONE shape serves all three statuses (schema/status/spec_digest/counts/
/// checks) so a consumer can iterate verifier["checks"] unconditionally —
/// only the refusal carries a "reason" instead of a seal (R2 P2-4).
LabVerifyOutcome indeterminateOutcome( const std::string &reason )
{
    LabVerifyOutcome outcome;
    outcome.ran = true;
    outcome.statusWord = "indeterminate";
    outcome.report = Json::Value( Json::objectValue );
    outcome.report["schema"] = kEvidenceSchema;
    outcome.report["status"] = outcome.statusWord;
    outcome.report["reason"] = reason;
    Json::Value counts( Json::objectValue );
    counts["pass"] = 0;
    counts["fail"] = 0;
    counts["indeterminate"] = 0;
    outcome.report["counts"] = counts;
    outcome.report["checks"] = Json::Value( Json::arrayValue );
    return outcome;
}

} // namespace

LabVerifyOutcome composeLabVerifyEvidence( const std::string &artifactPath,
                                           const std::string &artifactKind,
                                           const LabVerifyGridPins &pins )
{
    if ( artifactPath.empty() )
    {
        LabVerifyOutcome none;
        none.ran = false;
        return none;
    }

    // --- spec: the default teaching checks in declaration order -----------
    sicnu::verify::VerificationSpec spec;
    spec.specId = kTeachingSpecId;
    spec.scope = "task";

    sicnu::verify::VerificationCheckSpec existsCheck;
    existsCheck.checkId = "artifact.exists";
    existsCheck.kind = "artifact.exists";
    existsCheck.description = "the submitted artifact exists on disk at grade time";
    existsCheck.params = Json::Value( Json::objectValue );
    existsCheck.params["path"] = artifactPath;
    spec.checks.push_back( existsCheck );

    if ( !artifactKind.empty() )
    {
        sicnu::verify::VerificationCheckSpec typeCheck;
        typeCheck.checkId = "artifact.type";
        typeCheck.kind = "artifact.type";
        typeCheck.description = "the artifact is of the kind the lab rules declare";
        typeCheck.params = Json::Value( Json::objectValue );
        typeCheck.params["path"] = artifactPath;
        typeCheck.params["kind"] = artifactKind;
        spec.checks.push_back( typeCheck );
    }

    if ( pins.any() )
    {
        sicnu::verify::VerificationCheckSpec gridCheck;
        gridCheck.checkId = "artifact.grid";
        gridCheck.kind = "artifact.grid";
        gridCheck.description =
            "the declared grid contract (from the lab rules) holds independently of the grader";
        gridCheck.params = Json::Value( Json::objectValue );
        gridCheck.params["path"] = artifactPath;
        if ( pins.width > 0 )
            gridCheck.params["width"] = pins.width;
        if ( pins.height > 0 )
            gridCheck.params["height"] = pins.height;
        if ( pins.bandCount > 0 )
            gridCheck.params["bandCount"] = pins.bandCount;
        if ( !pins.crs.empty() )
            gridCheck.params["crs"] = pins.crs;
        spec.checks.push_back( gridCheck );
    }

    const std::vector<std::string> specErrors = sicnu::verify::validateSpec( spec );
    if ( !specErrors.empty() )
    {
        std::string joined = specErrors.front();
        for ( std::size_t i = 1; i < specErrors.size(); ++i )
            joined += "; " + specErrors[i];
        return indeterminateOutcome( "teaching spec rejected by the authority validator: " +
                                     joined );
    }

    // --- context: the probes this directory owns --------------------------
    // Digest budget ZERO on purpose: the teaching spec (exists / type /
    // grid) never reads ArtifactInfo::digest — only the reproducibility
    // check does, and it is not part of this spec. Hashing up to 64 MiB per
    // attach site for a field nothing consumes is pure cost (R2 P2-2).
    FsArtifactProbe artifactProbe( 0 );
    GdalGridProbe gridProbe;
    sicnu::verify::VerificationContext context;
    context.artifactProbe = &artifactProbe;
    context.gridProbe = &gridProbe;

    // --- authority run -----------------------------------------------------
    const sicnu::verify::VerificationReport report =
        sicnu::verify::evaluate( spec, context );

    LabVerifyOutcome outcome;
    outcome.ran = true;
    outcome.statusWord = sicnu::verify::statusToWire( report.overall );

    const Json::Value canonical = report.toCanonicalJson();
    outcome.report = Json::Value( Json::objectValue );
    outcome.report["schema"] = kEvidenceSchema;
    outcome.report["status"] = outcome.statusWord;
    outcome.report["spec_digest"] = report.specDigest;
    outcome.report["counts"] = canonical["counts"];
    outcome.report["checks"] = canonical["checks"];
    return outcome;
}

} // namespace sicnu::verify_adapters
