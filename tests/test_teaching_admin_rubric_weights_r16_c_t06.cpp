/***************************************************************************
  tests/test_teaching_admin_rubric_weights_r16_c_t06.cpp — R16-C/T06 WP3:
  rubric weight arithmetic must reject negative and non-finite weights.

  validateLabRules()/validateProcessRubric() only checked the AGGREGATE sum
  (lab rules: "sum > 0"; process rubric: |sum - 1.0| <= 1e-6). A negative
  weight cancels against a bloated partner and passes (a deduction-weight
  model where one assertion ADDS points by failing), and a non-finite sum
  slips through both checks (inf > 0 is true; fabs(NaN - 1.0) > eps is
  false). The grader would then compute scores from nonsense arithmetic —
  the validator is the console's only gate before a rubric reaches the
  grading authority.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/rubric_builder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <limits>

using namespace sicnu::teaching_admin;

namespace
{

bool hasIssue( const ValidationResult &r, const QString &code )
{
    for ( const auto &i : r.issues )
        if ( i.code == code )
            return true;
    return false;
}

QJsonObject labRules( const QJsonArray &assertions )
{
    return QJsonObject{
        { QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab.rules/1" ) },
        { QStringLiteral( "lab_id" ), QStringLiteral( "lab15" ) },
        { QStringLiteral( "title" ), QStringLiteral( "Lab 15" ) },
        { QStringLiteral( "artifact" ),
          QJsonObject{ { QStringLiteral( "kind" ), QStringLiteral( "raster" ) } } },
        { QStringLiteral( "assertions" ), assertions },
    };
}

QJsonObject assertion( const QString &id, double weight )
{
    return QJsonObject{
        { QStringLiteral( "id" ), id },
        { QStringLiteral( "kind" ), QStringLiteral( "range" ) },
        { QStringLiteral( "weight" ), weight },
        { QStringLiteral( "params" ), QJsonObject{} },
    };
}

// #1652-1 (R20-05): validateProcessRubric delegates to the grader's own
// GradingRubric::fromJson, so these helpers build the ENGINE dialect —
// dimensions[].criteria[] with maxPoints (weight == sum of maxPoints,
// dimension weights sum == totalPoints). The old flat criteria[] dialect was
// this file's own vocabulary; the engine never read it.
QJsonObject stageCriterion( const QString &id, double maxPoints )
{
    return QJsonObject{
        { QStringLiteral( "criterionId" ), id },
        { QStringLiteral( "maxPoints" ), maxPoints },
        { QStringLiteral( "kind" ), QStringLiteral( "stage" ) },
        { QStringLiteral( "evidenceKey" ), QStringLiteral( "process.stage" ) },
        { QStringLiteral( "stage" ),
          QJsonObject{ { QStringLiteral( "expectedState" ), QStringLiteral( "completed" ) },
                        { QStringLiteral( "minDistinct" ), 1 } } },
    };
}

QJsonObject processRubric( double dimensionWeight, double totalPoints, const QJsonArray &criteria )
{
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.grader.rubric/1" ) },
        { QStringLiteral( "rubricId" ), QStringLiteral( "r-t06" ) },
        { QStringLiteral( "revision" ), 1 },
        { QStringLiteral( "totalPoints" ), totalPoints },
        // The engine defaults passingScore to 60 and validates it against
        // totalPoints, so a small rubric must pin its own passing score.
        { QStringLiteral( "passingScore" ), 6.0 },
        { QStringLiteral( "dimensions" ),
          QJsonArray{ QJsonObject{
              { QStringLiteral( "dimensionId" ), QStringLiteral( "d1" ) },
              { QStringLiteral( "weight" ), dimensionWeight },
              { QStringLiteral( "criteria" ), criteria } } } },
    };
}

} // namespace

TEST_CASE( "lab rules reject a negative weight that keeps the sum positive",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // -20 + 120 sums to 100: aggregate-only checks pass, but a negative
    // deduction weight is nonsense arithmetic in the grader.
    const ValidationResult r = validateLabRules( labRules( QJsonArray{
        assertion( QStringLiteral( "a1" ), -20.0 ),
        assertion( QStringLiteral( "a2" ), 120.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "lab rules reject a non-finite weight sum (overflow)",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // 1e308 + 1e308 overflows to +inf: "sum > 0" holds, so the old check
    // waved it through.
    const ValidationResult r = validateLabRules( labRules( QJsonArray{
        assertion( QStringLiteral( "a1" ), 1e308 ),
        assertion( QStringLiteral( "a2" ), 1e308 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "weight_mismatch" ) ) );
}

TEST_CASE( "process rubric rejects a negative maxPoints criterion",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // The engine's weight model is maxPoints > 0 per criterion; a negative
    // member is nonsense arithmetic the engine refuses at parse time.
    const ValidationResult r = validateProcessRubric( processRubric( 10.0, 10.0, QJsonArray{
        stageCriterion( QStringLiteral( "c1" ), -2.0 ),
        stageCriterion( QStringLiteral( "c2" ), 12.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "process rubric rejects a NaN maxPoints",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // A NaN double serializes to JSON null; the engine's finiteNumberMember
    // refuses it — non-finite point arithmetic must never reach the grader.
    const ValidationResult r = validateProcessRubric( processRubric( 10.0, 10.0, QJsonArray{
        stageCriterion( QStringLiteral( "c1" ), std::numeric_limits<double>::quiet_NaN() ),
        stageCriterion( QStringLiteral( "c2" ), 10.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
}

TEST_CASE( "process rubric rejects a dimension weight that disagrees with its criteria",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // The engine's own weight invariant: dimension.weight == sum of its
    // criteria maxPoints (grader_types.cpp validate). 9 != 6 + 4.
    const ValidationResult r = validateProcessRubric( processRubric( 9.0, 10.0, QJsonArray{
        stageCriterion( QStringLiteral( "c1" ), 6.0 ),
        stageCriterion( QStringLiteral( "c2" ), 4.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "process rubric rejects dimension weights that disagree with totalPoints",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // sum of dimension weights (10) != totalPoints (12) — the engine refuses.
    const ValidationResult r = validateProcessRubric( processRubric( 10.0, 12.0, QJsonArray{
        stageCriterion( QStringLiteral( "c1" ), 6.0 ),
        stageCriterion( QStringLiteral( "c2" ), 4.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "finite positive weights still validate (no over-rejection)",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // Guard against hardening into refusal: the ordinary shapes stay ok.
    CHECK( validateLabRules( labRules( QJsonArray{
              assertion( QStringLiteral( "a1" ), 60.0 ),
              assertion( QStringLiteral( "a2" ), 40.0 ),
          } ) )
             .ok );
    // Engine dialect: 6 + 4 == dimension weight 10 == totalPoints.
    CHECK( validateProcessRubric( processRubric( 10.0, 10.0, QJsonArray{
              stageCriterion( QStringLiteral( "c1" ), 6.0 ),
              stageCriterion( QStringLiteral( "c2" ), 4.0 ),
          } ) )
             .ok );
}

TEST_CASE( "process rubric rejects a zero maxPoints criterion",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // The ENGINE's contract (not the old flat dialect's): maxPoints must be
    // strictly > 0 — a zero-point criterion carries no judgment and is
    // refused by the grading authority itself.
    const ValidationResult r = validateProcessRubric( processRubric( 10.0, 10.0, QJsonArray{
        stageCriterion( QStringLiteral( "c1" ), 0.0 ),
        stageCriterion( QStringLiteral( "c2" ), 10.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
}

TEST_CASE( "checkWeightSum stays fail-closed on a NaN member",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // Regression only: the live weight-sum helper already refuses NaN.
    const WeightSumCheck c =
      checkWeightSum( QJsonArray{ QJsonObject{ { QStringLiteral( "weight" ),
                                                std::numeric_limits<double>::quiet_NaN() } } } );
    CHECK_FALSE( c.matchesExpected );
}

TEST_CASE( "a string-typed weight is refused, not silently coerced to 0.0",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // Round-1 review P2: toDouble(0.0) turned a NON-NUMERIC weight ("0.5x",
    // true, null) into 0.0, which is finite and non-negative — a typo'd or
    // hostile rubric slid through the per-item rule that round WP3 added.
    // The weight must be a JSON number, on both rubric dialects.
    QJsonObject stringLab = assertion( QStringLiteral( "a1" ), 60.0 );
    stringLab.insert( QStringLiteral( "weight" ), QStringLiteral( "0.5x" ) );
    const ValidationResult lab = validateLabRules(
        labRules( QJsonArray{ stringLab, assertion( QStringLiteral( "a2" ), 40.0 ) } ) );
    REQUIRE_FALSE( lab.ok );
    CHECK( hasIssue( lab, QStringLiteral( "malformed_grading_rule" ) ) );

    QJsonObject stringProc = stageCriterion( QStringLiteral( "c1" ), 5.0 );
    stringProc.insert( QStringLiteral( "maxPoints" ), QStringLiteral( "5" ) );
    const ValidationResult proc = validateProcessRubric(
        processRubric( 10.0, 10.0, QJsonArray{ stringProc,
                                               stageCriterion( QStringLiteral( "c2" ), 5.0 ) } ) );
    REQUIRE_FALSE( proc.ok );
    CHECK( hasIssue( proc, QStringLiteral( "malformed_grading_rule" ) ) );
}
