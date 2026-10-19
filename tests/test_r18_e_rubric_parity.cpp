/***************************************************************************
  tests/test_r18_e_rubric_parity.cpp — R18-E E03/E08 rubric parity lane.

  T304 (D5): the admin-side validator (rubric_builder — preflight and the
  teaching admin dock both call it) is a SECOND, weaker weight truth than
  the grading authority (OutputVerifier::loadLabRules,
  src/agent/output_verifier.cpp): it accepted weight = 0, any positive sum,
  unknown severities and out-of-range passing scores, all of which the
  grader refuses at batch time with exit 2 — a green preflight blessing a
  rules file that fails an entire class into `error` rows. The validator
  must refuse exactly what the grader refuses.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/rubric_builder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

using namespace sicnu::teaching_admin;

namespace
{

QJsonObject validRules()
{
  QJsonObject rules;
  rules.insert( QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab.rules/1" ) );
  rules.insert( QStringLiteral( "lab_id" ), QStringLiteral( "labX" ) );
  rules.insert( QStringLiteral( "title" ), QStringLiteral( "Lab X" ) );
  QJsonObject artifact;
  artifact.insert( QStringLiteral( "kind" ), QStringLiteral( "raster" ) );
  rules.insert( QStringLiteral( "artifact" ), artifact );
  QJsonArray assertions;
  QJsonObject a1;
  a1.insert( QStringLiteral( "id" ), QStringLiteral( "band_count" ) );
  a1.insert( QStringLiteral( "kind" ), QStringLiteral( "band_layout" ) );
  a1.insert( QStringLiteral( "weight" ), 40.0 );
  a1.insert( QStringLiteral( "severity" ), QStringLiteral( "normal" ) );
  QJsonObject p1;
  p1.insert( QStringLiteral( "bands" ), 3 );
  a1.insert( QStringLiteral( "params" ), p1 );
  QJsonObject a2;
  a2.insert( QStringLiteral( "id" ), QStringLiteral( "mean_range" ) );
  a2.insert( QStringLiteral( "kind" ), QStringLiteral( "range" ) );
  a2.insert( QStringLiteral( "weight" ), 60.0 );
  a2.insert( QStringLiteral( "severity" ), QStringLiteral( "blocking" ) );
  QJsonObject p2;
  p2.insert( QStringLiteral( "min" ), 0.0 );
  p2.insert( QStringLiteral( "max" ), 1.0 );
  a2.insert( QStringLiteral( "params" ), p2 );
  assertions.append( a1 );
  assertions.append( a2 );
  rules.insert( QStringLiteral( "assertions" ), assertions );
  rules.insert( QStringLiteral( "passing_score" ), 60.0 );
  return rules;
}

bool hasErrorWith( const ValidationResult &r, const QString &code )
{
  for ( const auto &issue : r.issues )
    if ( issue.code == code && issue.severity == QStringLiteral( "error" ) )
      return true;
  return false;
}

} // namespace

TEST_CASE( "r18e T304: the shipped-rules shape still validates", "[rubric][r18_e]" )
{
  const ValidationResult r = validateLabRules( validRules() );
  CHECK( r.ok );
}

TEST_CASE( "r18e T304b: a zero weight is refused — the grader refuses it too",
           "[rubric][r18_e]" )
{
  // [40, 60, 0] sums to 100; the old validator allowed it, the grader
  // refuses weight <= 0 at batch time (exit 2, every row error).
  QJsonObject rules = validRules();
  QJsonArray assertions = rules.value( QStringLiteral( "assertions" ) ).toArray();
  QJsonObject zero = assertions.at( 1 ).toObject();
  zero.insert( QStringLiteral( "weight" ), 60.0 );
  assertions.replace( 1, zero );
  QJsonObject extra = assertions.at( 0 ).toObject();
  extra.insert( QStringLiteral( "id" ), QStringLiteral( "zero_weight" ) );
  extra.insert( QStringLiteral( "weight" ), 0.0 );
  assertions.append( extra );
  // re-balance to a clean 100 total: 40 + 60 + 0 = 100
  rules.insert( QStringLiteral( "assertions" ), assertions );

  const ValidationResult r = validateLabRules( rules );
  CHECK( hasErrorWith( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "r18e T304c: weights must sum to exactly 100, not merely be positive",
           "[rubric][r18_e]" )
{
  QJsonObject rules = validRules();
  QJsonArray assertions = rules.value( QStringLiteral( "assertions" ) ).toArray();
  QJsonObject scaled = assertions.at( 0 ).toObject();
  scaled.insert( QStringLiteral( "weight" ), 0.4 );
  assertions.replace( 0, scaled );
  QJsonObject scaled2 = assertions.at( 1 ).toObject();
  scaled2.insert( QStringLiteral( "weight" ), 0.6 );
  assertions.replace( 1, scaled2 );
  rules.insert( QStringLiteral( "assertions" ), assertions );

  const ValidationResult r = validateLabRules( rules );
  CHECK( hasErrorWith( r, QStringLiteral( "weight_mismatch" ) ) );
}

TEST_CASE( "r18e T304d: an unknown severity is refused", "[rubric][r18_e]" )
{
  QJsonObject rules = validRules();
  QJsonArray assertions = rules.value( QStringLiteral( "assertions" ) ).toArray();
  QJsonObject a = assertions.at( 0 ).toObject();
  a.insert( QStringLiteral( "severity" ), QStringLiteral( "severe" ) );
  assertions.replace( 0, a );
  rules.insert( QStringLiteral( "assertions" ), assertions );

  const ValidationResult r = validateLabRules( rules );
  CHECK( hasErrorWith( r, QStringLiteral( "malformed_grading_rule" ) ) );
}

TEST_CASE( "r18e T304e: passing_score outside [0,100] is refused", "[rubric][r18_e]" )
{
  QJsonObject rules = validRules();
  rules.insert( QStringLiteral( "passing_score" ), 150.0 );
  CHECK( hasErrorWith( validateLabRules( rules ), QStringLiteral( "malformed_grading_rule" ) ) );

  QJsonObject negative = validRules();
  negative.insert( QStringLiteral( "passing_score" ), -5.0 );
  CHECK( hasErrorWith( validateLabRules( negative ), QStringLiteral( "malformed_grading_rule" ) ) );
}
