// tests/test_r16_a_grader.cpp — R16-A T02 regressions for the grader's
// canonical-walk recursion budget.
//
// grader_json's appendCanonical recursed per nesting level with NO depth
// budget. Documents that arrive through parseJsonStrict are bounded by the
// jsoncpp reader's default stackLimit (256 nesting levels — probed; deeper
// text throws "Exceeded stackLimit" and is caught into a typed refusal),
// but the engine is also a programmatic API: GradeEvidence value objects
// are public, and evidence facts copied verbatim into toJson() reached the
// unbudgeted walk — a ~100k-deep facts value crashed canonicalizeJson with
// SIGSEGV inside grade()'s digest step (probe transcript, exit 139).
//
// The budget is set at 512 canonical levels: double the reader's parse
// bound, so every parse-legal document stays canonicalizable, and far
// below any stack danger. The refusal rides the EXISTING GraderError
// channel; the engine already maps digest failures to a typed
// SchemaShapeInvalid refusal with no report.
#include "grader/grader_engine.h"
#include "grader/grader_json.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <string>

using namespace sicnu::grader;

namespace
{

/// A `depth`-deep object chain built in O(depth) through stable member
/// references (jsoncpp's object map is node-based). Depth is kept at 2000 —
// jsoncpp's own Value destructor recurses per level too, so a 100k-level
// value must not even be constructed in-process (the crash itself was
// demonstrated by the standalone probe, not here).
Json::Value deepChain( int depth )
{
    Json::Value root( Json::objectValue );
    Json::Value *cursor = &root;
    for ( int i = 0; i < depth; ++i )
    {
        Json::Value &next = ( *cursor )["n"];
        if ( !next.isObject() )
            next = Json::Value( Json::objectValue );
        cursor = &next;
    }
    ( *cursor )["leaf"] = 1;
    return root;
}

/// One-criterion stage rubric (weights balanced).
GradingRubric oneStageRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "r16a-depth";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    Dimension dimension;
    dimension.dimensionId = "d1";
    dimension.weight = 100.0;
    Criterion criterion;
    criterion.criterionId = "c1";
    criterion.maxPoints = 100.0;
    criterion.kind = CriterionKind::Stage;
    criterion.evidenceKey = "train";
    criterion.stage.expectedState = "Completed";
    dimension.criteria.push_back( criterion );
    rubric.dimensions.push_back( dimension );
    return rubric;
}

} // namespace

TEST_CASE( "r16-a grader: canonicalizeJson refuses pathologically deep values typed",
           "[grader][r16a][canonical]" )
{
    // Parse-legal depth (the reader's own bound) still canonicalizes.
    GraderError error;
    std::string legal;
    CHECK( canonicalizeJson( deepChain( 256 ), error ).has_value() );
    CHECK( error.ok() );

    // Over-budget depth is a typed refusal, never a stack overflow.
    Json::Value bomb = deepChain( 2000 );
    const auto canonical = canonicalizeJson( bomb, error );
    CHECK_FALSE( canonical.has_value() );
    CHECK( error.code == GraderErrorCode::SchemaShapeInvalid );
    CHECK( error.message.find( "depth" ) != std::string::npos );
}

TEST_CASE( "r16-a grader: deep evidence facts are a typed refusal, not a crash in grade()",
           "[grader][r16a][canonical]" )
{
    GradingRubric rubric = oneStageRubric();

    GradeEvidence evidence;
    GradeEvidenceItem item;
    item.evidenceId = "ev-1";
    item.kind = EvidenceKind::ArtifactState;
    item.key = "artifact";
    item.state = "present";
    item.facts["deep"] = deepChain( 2000 );
    evidence.items.push_back( item );

    const GradeOutcome outcome = grade( rubric, evidence );
    CHECK_FALSE( outcome.ok );
    REQUIRE( outcome.error.code == GraderErrorCode::SchemaShapeInvalid );
    CHECK( outcome.error.message.find( "canonicalized" ) != std::string::npos );

    // The legal twin grades normally (the same shape at a legal depth).
    GradeEvidence sane;
    GradeEvidenceItem flat;
    flat.evidenceId = "ev-1";
    flat.kind = EvidenceKind::ArtifactState;
    flat.key = "artifact";
    flat.state = "present";
    flat.facts["deep"] = deepChain( 8 );
    sane.items.push_back( flat );
    GradeEvidenceItem stage;
    stage.evidenceId = "ev-stage";
    stage.kind = EvidenceKind::Stage;
    stage.key = "train";
    stage.state = "Completed";
    sane.items.push_back( stage );
    const GradeOutcome ok = grade( rubric, sane );
    REQUIRE( ok.ok );
    CHECK( ok.report.verdict == ReportVerdict::Pass );
}
