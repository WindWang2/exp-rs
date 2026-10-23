// R20-05 #1652-1 RED driver: an ENGINE-legal rubric must validate ok, and a
// FLAT-dialect rubric (old admin vocabulary) must be refused.
#include "teaching_admin/rubric_builder.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

static QJsonObject doc( const char *text )
{
    return QJsonDocument::fromJson( QByteArray( text ) ).object();
}

int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );
    using namespace sicnu::teaching_admin;

    // Engine dialect, self-consistent: weight == sum(maxPoints) == totalPoints.
    const QJsonObject engineLegal = doc( R"({
      "schema": "sicnu.grader.rubric/1",
      "rubricId": "r-drv",
      "revision": 1,
      "totalPoints": 10.0,
      "passingScore": 6.0,
      "dimensions": [ {
        "dimensionId": "d1", "weight": 10.0,
        "criteria": [
          { "criterionId": "c1", "maxPoints": 6.0, "kind": "stage",
            "evidenceKey": "process.stage",
            "stage": { "expectedState": "completed", "minDistinct": 1 } },
          { "criterionId": "c2", "maxPoints": 4.0, "kind": "metric",
            "evidenceKey": "metric.oa",
            "metric": { "mode": "at_least", "value": 0.8 } } ] } ]
    })" );
    const ValidationResult legal = validateProcessRubric( engineLegal );
    std::cout << "engine-legal rubric -> ok=" << ( legal.ok ? 1 : 0 ) << "\n";

    // The OLD admin flat dialect: top-level criteria, weights sum 1.0.
    const QJsonObject flat = doc( R"({
      "schema": "sicnu.grader.rubric/1",
      "criteria": [
        { "id": "c1", "kind": "stage", "weight": 0.5 },
        { "id": "c2", "kind": "metric", "weight": 0.5, "metricKey": "oa" } ]
    })" );
    const ValidationResult flatResult = validateProcessRubric( flat );
    std::cout << "flat-dialect rubric -> ok=" << ( flatResult.ok ? 1 : 0 ) << "\n";

    const bool pass = legal.ok && !flatResult.ok;
    std::cout << ( pass ? "DELEGATION-OK (engine truth)\n"
                        : "OLD-BEHAVIOR (flat dialect still the validator's truth)\n" );
    return pass ? 0 : 1;
}
