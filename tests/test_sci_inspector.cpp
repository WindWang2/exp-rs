// RS14 Scientific Inspector (ADR 0172) — Slice A view-model contract tests.
//
// Pure value + codec tests: no QGIS, no widgets, no stores. The codec is the
// machine-readable exit (agent-facing) — determinism and boundary strictness
// are the contract, not conveniences. Unknown/garbage vocabulary degrades
// truthfully to Unknown; the JSON boundary refuses foreign envelopes.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/scientific/sci_fact.h"
#include "app/workbench/scientific/sci_inspection.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QTime>

#include <vector>

using namespace sicnu::app::sci;

namespace
{

SciFact sampleFact()
{
  SciFact f;
  f.key = QStringLiteral( "passport.driver" );
  f.label = QStringLiteral( "驱动" );
  f.value = QStringLiteral( "GTiff" );
  f.status = FactStatus::Observed;
  f.source = QStringLiteral( "data.manager" );
  return f;
}

SciSectionReport sampleSection()
{
  SciSectionReport s;
  s.id = QStringLiteral( "passport" );
  s.title = QStringLiteral( "数据护照" );
  s.facts.push_back( sampleFact() );
  return s;
}

ScientificInspection sampleInspection()
{
  ScientificInspection i;
  i.schemaVersion = kSciInspectionSchemaVersion;
  i.generatedAtUtc = QDateTime( QDate( 2026, 9, 21 ), QTime( 1, 2, 3 ), QTimeZone::UTC );
  i.target.kind = QStringLiteral( "asset" );
  i.target.id = QStringLiteral( "asset-1" );
  i.target.displayLabel = QStringLiteral( "demo.tif" );
  i.sections.push_back( sampleSection() );
  return i;
}

QJsonObject sampleInspectionJsonRoot()
{
  return inspectionToJson( sampleInspection() ).object();
}

} // namespace

TEST_CASE( "FactStatus wire vocabulary", "[sci][inspector]" )
{
  SECTION( "round trips all six statuses" )
  {
    const FactStatus all[] = { FactStatus::Observed, FactStatus::Inferred, FactStatus::Assumed,
                               FactStatus::Unknown, FactStatus::Conflict };
    for ( const FactStatus status : all )
    {
      const QString wire = factStatusToWire( status );
      REQUIRE_FALSE( wire.isEmpty() );
      REQUIRE( factStatusFromAuthoritative( wire ) == status );
    }
  }

  SECTION( "harness vocabulary maps: derived → Inferred (documented rename)" )
  {
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "observed" ) ) == FactStatus::Observed );
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "derived" ) ) == FactStatus::Inferred );
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "assumed" ) ) == FactStatus::Assumed );
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "unknown" ) ) == FactStatus::Unknown );
  }

  SECTION( "garbage degrades truthfully to Unknown, never to a strong claim" )
  {
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "obsered" ) ) == FactStatus::Unknown );
    REQUIRE( factStatusFromAuthoritative( QString() ) == FactStatus::Unknown );
    REQUIRE( factStatusFromAuthoritative( QStringLiteral( "excellent" ) ) == FactStatus::Unknown );
  }
}

TEST_CASE( "ScientificInspection JSON envelope", "[sci][inspector][codec]" )
{
  const QJsonDocument doc = inspectionToJson( sampleInspection() );
  REQUIRE( doc.isObject() );
  const QJsonObject root = doc.object();

  SECTION( "kind, schema version, target and timestamp survive" )
  {
    REQUIRE( root.value( QLatin1String( "kind" ) ).toString() == QStringLiteral( "sci_inspection" ) );
    REQUIRE( root.value( QLatin1String( "schema_version" ) ).toString()
             == QStringLiteral( "1.0" ) );
    const QJsonObject target = root.value( QLatin1String( "target" ) ).toObject();
    REQUIRE( target.value( QLatin1String( "kind" ) ).toString() == QStringLiteral( "asset" ) );
    REQUIRE( target.value( QLatin1String( "id" ) ).toString() == QStringLiteral( "asset-1" ) );
    const QDateTime parsed = QDateTime::fromString(
      root.value( QLatin1String( "generated_at_utc" ) ).toString(), Qt::ISODateWithMs );
    REQUIRE( parsed.isValid() );
    REQUIRE( parsed.toUTC() == sampleInspection().generatedAtUtc );
  }

  SECTION( "sections carry facts with wire statuses" )
  {
    const QJsonArray sections = root.value( QLatin1String( "sections" ) ).toArray();
    REQUIRE( sections.size() == 1 );
    const QJsonObject passport = sections.at( 0 ).toObject();
    REQUIRE( passport.value( QLatin1String( "id" ) ).toString() == QStringLiteral( "passport" ) );
    REQUIRE( passport.value( QLatin1String( "available" ) ).toBool() );
    const QJsonArray facts = passport.value( QLatin1String( "facts" ) ).toArray();
    REQUIRE( facts.size() == 1 );
    const QJsonObject fact = facts.at( 0 ).toObject();
    REQUIRE( fact.value( QLatin1String( "key" ) ).toString() == QStringLiteral( "passport.driver" ) );
    REQUIRE( fact.value( QLatin1String( "status" ) ).toString() == QStringLiteral( "observed" ) );
  }

  SECTION( "deterministic serialization (agent replay contract)" )
  {
    const QByteArray first = doc.toJson( QJsonDocument::Compact );
    const QByteArray second = inspectionToJson( sampleInspection() ).toJson( QJsonDocument::Compact );
    REQUIRE( first == second );
  }
}

TEST_CASE( "ScientificInspection codec round trip", "[sci][inspector][codec]" )
{
  ScientificInspection inspection = sampleInspection();
  SciSectionReport geometry;
  geometry.id = QStringLiteral( "geometry" );
  geometry.title = QStringLiteral( "几何/网格" );
  geometry.facts.push_back( sampleFact() );
  SciFinding finding;
  finding.code = QStringLiteral( "sci.grid.no_geotransform" );
  finding.severity = sicnu::data::DiagnosticSeverity::Warning;
  finding.message = QStringLiteral( "缺少地理变换" );
  finding.evidence = QStringLiteral( "hasGeoTransform=false" );
  geometry.findings.push_back( finding );
  geometry.truncated = true;
  geometry.truncationNote = QStringLiteral( "超出 32 条上限" );
  inspection.sections.push_back( geometry );

  const QJsonDocument doc = inspectionToJson( inspection );
  const sicnu::data::Result<ScientificInspection> back = inspectionFromJson( doc );
  REQUIRE( back.has_value() );

  SECTION( "value-preserving round trip" )
  {
    const ScientificInspection &value = back.value();
    REQUIRE( value.schemaVersion == inspection.schemaVersion );
    REQUIRE( value.target == inspection.target );
    REQUIRE( value.generatedAtUtc == inspection.generatedAtUtc );
    REQUIRE( value.sections.size() == inspection.sections.size() );
    REQUIRE( value.sections.at( 1 ).id == QStringLiteral( "geometry" ) );
    REQUIRE( value.sections.at( 1 ).truncated );
    REQUIRE( value.sections.at( 1 ).truncationNote == QStringLiteral( "超出 32 条上限" ) );
    REQUIRE( value.sections.at( 1 ).findings.size() == 1 );
    REQUIRE( value.sections.at( 1 ).findings.at( 0 ).code
             == QStringLiteral( "sci.grid.no_geotransform" ) );
    REQUIRE( value.sections.at( 1 ).findings.at( 0 ).severity
             == sicnu::data::DiagnosticSeverity::Warning );
  }

  SECTION( "re-serialization is byte-identical (deterministic persistence)" )
  {
    const QByteArray once = inspectionToJson( back.value() ).toJson( QJsonDocument::Compact );
    const QByteArray twice = doc.toJson( QJsonDocument::Compact );
    REQUIRE( once == twice );
  }
}

TEST_CASE( "ScientificInspection codec rejects foreign or broken envelopes", "[sci][inspector][codec]" )
{
  auto expectFailure = []( const QJsonDocument &doc, const QString &code ) {
    const sicnu::data::Result<ScientificInspection> result = inspectionFromJson( doc );
    REQUIRE_FALSE( result.has_value() );
    bool found = false;
    for ( const sicnu::data::Diagnostic &d : result.diagnostics() )
    {
      if ( d.code == code )
        found = true;
    }
    INFO( "expected diagnostic code: " << code.toStdString() );
    REQUIRE( found );
  };

  SECTION( "not an object" )
  {
    expectFailure( QJsonDocument( QJsonArray() ), QStringLiteral( "sci.codec.envelope" ) );
  }

  SECTION( "missing schema_version" )
  {
    QJsonObject root = sampleInspectionJsonRoot();
    root.remove( QLatin1String( "schema_version" ) );
    expectFailure( QJsonDocument( root ), QStringLiteral( "sci.codec.schema" ) );
  }

  SECTION( "foreign kind is refused, not guessed" )
  {
    QJsonObject root = sampleInspectionJsonRoot();
    root.insert( QLatin1String( "kind" ), QStringLiteral( "d17_provenance" ) );
    expectFailure( QJsonDocument( root ), QStringLiteral( "sci.codec.kind" ) );
  }

  SECTION( "foreign schema version is refused" )
  {
    QJsonObject root = sampleInspectionJsonRoot();
    root.insert( QLatin1String( "schema_version" ), QStringLiteral( "9.9" ) );
    expectFailure( QJsonDocument( root ), QStringLiteral( "sci.codec.version" ) );
  }

  SECTION( "unparseable timestamp" )
  {
    QJsonObject root = sampleInspectionJsonRoot();
    root.insert( QLatin1String( "generated_at_utc" ), QStringLiteral( "yesterday" ) );
    expectFailure( QJsonDocument( root ), QStringLiteral( "sci.codec.timestamp" ) );
  }

  SECTION( "invalid fact status inside an envelope" )
  {
    QJsonObject root = sampleInspectionJsonRoot();
    QJsonObject passport =
      root.value( QLatin1String( "sections" ) ).toArray().at( 0 ).toObject();
    QJsonArray facts = passport.value( QLatin1String( "facts" ) ).toArray();
    QJsonObject fact = facts.at( 0 ).toObject();
    fact.insert( QLatin1String( "status" ), QStringLiteral( "obsered" ) );
    facts.replace( 0, fact );
    passport.insert( QLatin1String( "facts" ), facts );
    QJsonArray sections;
    sections.append( passport );
    root.insert( QLatin1String( "sections" ), sections );
    expectFailure( QJsonDocument( root ), QStringLiteral( "sci.codec.status" ) );
  }
}

TEST_CASE( "Unavailable and truncated sections keep typed state in the envelope",
           "[sci][inspector][codec]" )
{
  ScientificInspection inspection = sampleInspection();
  SciSectionReport quality;
  quality.id = QStringLiteral( "quality" );
  quality.title = QStringLiteral( "质量" );
  quality.available = false;
  quality.unavailableReason = QStringLiteral( "workspace service 不可用" );
  inspection.sections.push_back( quality );

  const sicnu::data::Result<ScientificInspection> back =
    inspectionFromJson( inspectionToJson( inspection ) );
  REQUIRE( back.has_value() );
  REQUIRE( back.value().sections.size() == 2 );
  REQUIRE_FALSE( back.value().sections.at( 1 ).available );
  REQUIRE( back.value().sections.at( 1 ).unavailableReason
           == QStringLiteral( "workspace service 不可用" ) );
  REQUIRE( back.value().sections.at( 1 ).facts.empty() );
}
