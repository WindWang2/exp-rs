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

QJsonObject processRubric( const QJsonArray &criteria )
{
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.grader.rubric/1" ) },
        { QStringLiteral( "criteria" ), criteria },
    };
}

QJsonObject criterion( const QString &id, double weight )
{
    return QJsonObject{
        { QStringLiteral( "id" ), id },
        { QStringLiteral( "kind" ), QStringLiteral( "stage" ) },
        { QStringLiteral( "weight" ), weight },
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

TEST_CASE( "process rubric rejects a negative weight that keeps the sum at 1.0",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    const ValidationResult r = validateProcessRubric( processRubric( QJsonArray{
        criterion( QStringLiteral( "c1" ), -0.5 ),
        criterion( QStringLiteral( "c2" ), 1.5 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "process rubric rejects a NaN weight",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // sum becomes NaN and |NaN - 1.0| > 1e-6 is FALSE — the old aggregate
    // check silently accepted it.
    const ValidationResult r = validateProcessRubric( processRubric( QJsonArray{
        criterion( QStringLiteral( "c1" ), std::numeric_limits<double>::quiet_NaN() ),
        criterion( QStringLiteral( "c2" ), 1.0 ),
    } ) );
    REQUIRE_FALSE( r.ok );
}

TEST_CASE( "process rubric rejects an infinite weight sum",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    const ValidationResult r = validateProcessRubric( processRubric( QJsonArray{
        criterion( QStringLiteral( "c1" ), 1e308 ),
        criterion( QStringLiteral( "c2" ), 1e308 ),
    } ) );
    REQUIRE_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "weight_mismatch" ) ) );
}

TEST_CASE( "finite non-negative weights still validate (no over-rejection)",
           "[teaching_admin][rubric][r16_c_t06]" )
{
    // Guard against hardening into refusal: the ordinary shapes stay ok.
    CHECK( validateLabRules( labRules( QJsonArray{
              assertion( QStringLiteral( "a1" ), 60.0 ),
              assertion( QStringLiteral( "a2" ), 40.0 ),
          } ) )
             .ok );
    // A zero weight is finite and non-negative — allowed (existing behavior).
    CHECK( validateProcessRubric( processRubric( QJsonArray{
              criterion( QStringLiteral( "c1" ), 0.0 ),
              criterion( QStringLiteral( "c2" ), 1.0 ),
          } ) )
             .ok );
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

    QJsonObject stringProc = criterion( QStringLiteral( "c1" ), 0.5 );
    stringProc.insert( QStringLiteral( "weight" ), QStringLiteral( "0.5" ) );
    const ValidationResult proc = validateProcessRubric(
        processRubric( QJsonArray{ stringProc, criterion( QStringLiteral( "c2" ), 0.5 ) } ) );
    REQUIRE_FALSE( proc.ok );
    CHECK( hasIssue( proc, QStringLiteral( "malformed_grading_rule" ) ) );
}
