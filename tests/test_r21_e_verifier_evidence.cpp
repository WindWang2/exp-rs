// tests/test_r21_e_verifier_evidence.cpp — R21-E W1 (#1601): the production
// lab grade report carries scientific-verifier evidence from the ADR 0172
// authority (src/verify), not just the grader's own assertions.
//
// RED (against master 911e0b17): gradeForTeaching() produces no verifier
// evidence at all — the authority core has zero production consumers, so
// body.isMember("verifier") is false and every CHECK below fails.
//
// GREEN contract (W1):
//   * body["verifier"] is present for every graded (and artifact-error)
//     result, absent for usage-class refusals (unknown lab / missing rules /
//     missing artifact — nothing to verify);
//   * verifier.status ∈ {"pass","fail","indeterminate"} — three-valued, an
//     Indeterminate NEVER folds into pass;
//   * verifier.checks[] carries the authority's per-check facts and
//     verifier.spec_digest seals them (sha256 hex, 64 chars);
//   * identical input grades byte-identical verifier evidence.

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QString>

#include "agent/output_verifier.h"
#include "geospatial/crs/crs_policy.h"

#include <string>

using sicnu::agent::OutputVerifier;

namespace
{
/// Capability probe (R2 P1-2): grading resolves CRS through OSR, which needs
/// a locatable PROJ database. On a host without one the lane is not
/// testable — skip honestly instead of failing red for an environment gap.
bool projDataAvailable()
{
    return sicnu::geo::Crs::fromAuthid( "EPSG:4326" ).isValid();
}

const char *fixturesDir()
{
    return CMAKE_SOURCE_DIR "/tests/fixtures/lab";
}

std::string fixturePath( const char *name )
{
    return std::string( fixturesDir() ) + "/" + name;
}

/// Explicit rules dir (the repo convention, tests/test_lab_grading.cpp:52):
/// SICNU_SOURCE_DIR is a PRIVATE definition of sicnu_agent, so the
/// source-tree fallback is invisible from a test binary — without this the
/// lab id would resolve "unknown lab" (usage class) and the graded cases
/// would vacuously pass through the usage branch.
OutputVerifier::LabGradeResult grade( const char *lab, const char *fixture )
{
    OutputVerifier::LabGradeOptions options;
    options.rulesDir = QString::fromLatin1( CMAKE_SOURCE_DIR "/data/labs/grading" );
    const OutputVerifier verifier;
    return verifier.gradeArtifact( QString::fromLatin1( lab ),
                                   QString::fromLatin1( fixturePath( fixture ) ), options );
}

bool isSeal( const std::string &text )
{
    if ( text.size() != 64 )
        return false;
    for ( const char c : text )
        if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) )
            return false;
    return true;
}
} // namespace

TEST_CASE( "verifier evidence: graded report carries authority evidence", "[r21e][verifier_evidence]" )
{
    if ( !projDataAvailable() )
    {
        WARN( "PROJ database not locatable on this host — grading cannot "
              "resolve CRS; lane not testable here (see R2 P1-2)" );
        return;
    }
    const auto result = grade( "ndvi_basics", "ndvi_basics_reference.tif" );
    REQUIRE( result.graded );
    REQUIRE( result.verdict == "pass" );

    const Json::Value body = result.toBodyJson();
    CHECK( body.isMember( "verifier" ) );
    if ( !body.isMember( "verifier" ) )
        return; // RED short-circuit: keep the failure output readable

    const Json::Value &verifier = body["verifier"];
    CHECK( verifier.isObject() );
    CHECK( verifier["schema"].asString() == "sicnu.verifier.evidence/1" );
    const std::string status = verifier["status"].asString();
    INFO( "verifier status: " << status );
    CHECK( ( status == "pass" || status == "fail" || status == "indeterminate" ) );
    // A fully readable reference artifact probed by the fs+grid adapters must
    // not read as "could not verify" — but it must also never be guessed.
    CHECK( status == "pass" );
    CHECK( verifier["checks"].isArray() );
    CHECK( verifier["checks"].size() >= 2 ); // exists + type (+ grid pins)
    CHECK( isSeal( verifier["spec_digest"].asString() ) );
}

TEST_CASE( "verifier evidence: indeterminate never reads as pass",
           "[r21e][verifier_evidence]" )
{
    // A corrupt "raster" (text bytes) is gradeable input to the composer: the
    // fs probe can see the file exists; the grid probe cannot answer. The
    // authority's fail-closed lattice must surface that as evidence, never as
    // a pass — and never silently absent.
    const std::string path = [] {
        const QString tmp = QDir::temp().absoluteFilePath( "r21e_corrupt_raster.tif" );
        QFile file( tmp );
        file.open( QIODevice::WriteOnly );
        file.write( "this is not a raster, not even close" );
        file.close();
        return tmp.toStdString();
    }();

    OutputVerifier::LabGradeOptions options;
    options.rulesDir = QString::fromLatin1( CMAKE_SOURCE_DIR "/data/labs/grading" );
    const OutputVerifier verifier;
    const auto result = verifier.gradeArtifact( QString::fromLatin1( "ndvi_basics" ),
                                                QString::fromStdString( path ), options );
    // The grader itself refuses the artifact (artifact class) — but the
    // verifier evidence must still be present and NOT "pass".
    CHECK( !result.graded );
    const Json::Value body = result.toBodyJson();
    CHECK( body.isMember( "verifier" ) );
    if ( !body.isMember( "verifier" ) )
        return;
    const std::string status = body["verifier"]["status"].asString();
    INFO( "verifier status on corrupt artifact: " << status );
    CHECK( status != "pass" );
}

TEST_CASE( "verifier evidence: usage refusals carry no evidence", "[r21e][verifier_evidence]" )
{
    const auto unknownLab = grade( "no_such_lab_r21e", "ndvi_basics_reference.tif" );
    CHECK( !unknownLab.graded );
    CHECK( unknownLab.errorClass == "usage" );
    CHECK( !unknownLab.toBodyJson().isMember( "verifier" ) );

    OutputVerifier::LabGradeOptions options;
    options.rulesDir = QString::fromLatin1( CMAKE_SOURCE_DIR "/data/labs/grading" );
    const OutputVerifier verifier;
    const auto missingArtifact = verifier.gradeArtifact(
      QString::fromLatin1( "ndvi_basics" ),
      QString::fromLatin1( "/no/such/dir/r21e_missing.tif" ), options );
    CHECK( !missingArtifact.graded );
    CHECK( missingArtifact.errorClass == "usage" );
    CHECK( !missingArtifact.toBodyJson().isMember( "verifier" ) );
}

TEST_CASE( "verifier evidence: identical input is byte-identical",
           "[r21e][verifier_evidence]" )
{
    if ( !projDataAvailable() )
    {
        WARN( "PROJ database not locatable on this host — lane not testable here" );
        return;
    }
    const auto first = grade( "ndvi_basics", "ndvi_basics_reference.tif" );
    const auto second = grade( "ndvi_basics", "ndvi_basics_reference.tif" );
    REQUIRE( first.graded );
    REQUIRE( second.graded );
    if ( !first.toBodyJson().isMember( "verifier" ) )
    {
        FAIL( "RED: no verifier evidence on master — determinism unmeasurable" );
    }
    CHECK( first.toBodyJson()["verifier"] == second.toBodyJson()["verifier"] );
}
