/***************************************************************************
  tests/test_teaching_authoring_failures_r4.cpp — WP-F (Track 12, teaching-lab
  R4): authoring-family failure paths + lab fixture/pack asset consistency.

  The fixture-asset cases go through the SAME inventory entry the admin
  console uses (inventoryPacks — the WP-A boundary entry); no second pack
  validator is created here. All authoring rejections must carry the typed
  codes of the existing vocabulary (labspec_authoring / rubric_builder /
  curriculum_editor), with the offending path in the message.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/curriculum_editor.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/labspec_authoring.h"
#include "teaching_admin/operator_catalog.h"
#include "teaching_admin/release_preflight.h"
#include "teaching_admin/rubric_builder.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

QJsonObject qjson( const char *text )
{
    return QJsonDocument::fromJson( QByteArray( text ) ).object();
}

bool hasIssue( const ValidationResult &r, const QString &code, const QString &pathPart )
{
    for ( const AdminIssue &issue : r.issues )
        if ( issue.code == code && ( pathPart.isEmpty() || issue.path.contains( pathPart ) ) )
            return true;
    return false;
}

void writeBytes( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
}

/// Minimal pack document declaring one committed-fixture input.
QByteArray packJson( const QString &assetRelPath, const char *sha256Hex )
{
    const QByteArray doc = QStringLiteral(
      R"JSON({
        "schema_version": "sicnu.lab-pack/1",
        "lab_id": "lab90_r4",
        "pack_version": "1.0.0",
        "license": "test",
        "inputs": [
          {
            "path": "%1",
            "role": "aux",
            "provenance": "committed-fixture",
            "sha256": "%2",
            "bytes": 1881,
            "sensor_truth": "fixture for the r4 boundary suite"
          }
        ]
      })JSON" )
                             .arg( assetRelPath, QString::fromLatin1( sha256Hex ) )
                             .toUtf8();
    return doc;
}

constexpr const char *kAnySha = "ad081257ef39112d0af3207f1697a3b2ad89042e2ca728e14864b7cdaac4467d";

} // namespace

TEST_CASE( "authoring r4: traversal-shaped authoring refs are typed unsafe_ref",
           "[teaching_r4][authoring][labspec]" )
{
    QTemporaryDir root;
    const QJsonObject spec = qjson( R"JSON({
      "spec_version": 1,
      "id": "lab90_r4",
      "grading_ref": { "intent_ref": "../outside/grade_intent.json" }
    })JSON" );

    const ValidationResult r = validateLabSpec( spec, QSet<QString>{ "rs:ndvi" }, {}, root.path() );
    CHECK_FALSE( r.ok );
    CHECK( hasIssue( r, QStringLiteral( "unsafe_ref" ), QStringLiteral( "grading_ref" ) ) );
}

TEST_CASE( "authoring r4: duplicate ids in rules and curriculum are typed per position",
           "[teaching_r4][authoring]" )
{
    // Duplicate assertion ids in a lab rules document.
    const ValidationResult rules = validateLabRules( qjson( R"JSON({
      "schema_version": "sicnu.lab.rules/1",
      "lab_id": "lab90_r4",
      "title": "t",
      "assertions": [
        { "id": "a1", "kind": "band_count", "params": {}, "weight": 0.5 },
        { "id": "a1", "kind": "band_count", "params": {}, "weight": 0.5 }
      ]
    })JSON" ) );
    CHECK_FALSE( rules.ok );
    CHECK( hasIssue( rules, QStringLiteral( "duplicate_assertion_id" ), QString() ) );

    // Duplicate module id AND duplicate module index in one curriculum.
    const ValidationResult curriculum = validateCurriculum(
      qjson( R"JSON({
        "schema": "sicnu.curriculum/1",
        "course_id": "c1",
        "title_zh": "课程",
        "mystery_key": true,
        "modules": [
          { "id": "m1", "index": 1, "title_zh": "一", "labs": [] },
          { "id": "m1", "index": 1, "title_zh": "二", "labs": [] }
        ]
      })JSON" ),
      QSet<QString>{}, QSet<QString>{} );
    CHECK_FALSE( curriculum.ok );
    CHECK( hasIssue( curriculum, QStringLiteral( "duplicate_module_id" ), QString() ) );
    CHECK( hasIssue( curriculum, QStringLiteral( "duplicate_module_index" ), QString() ) );
    CHECK( hasIssue( curriculum, QStringLiteral( "unknown_key" ), QString() ) );
}

TEST_CASE( "authoring r4: process rubric requires a metric per metric-kind criterion",
           "[teaching_r4][authoring][rubric]" )
{
    const ValidationResult rubric = validateProcessRubric( qjson( R"JSON({
      "schema": "sicnu.grader.rubric/1",
      "criteria": [
        { "id": "c1", "kind": "metric", "weight": 1.0 }
      ]
    })JSON" ) );
    CHECK_FALSE( rubric.ok );
    CHECK( hasIssue( rubric, QStringLiteral( "missing_metric" ), QStringLiteral( "criteria[0]" ) ) );
}

TEST_CASE( "authoring r4: preflight report keeps ok=false with a stable canonical digest",
           "[teaching_r4][authoring][preflight]" )
{
    PreflightInput in;
    in.knownOperators = QSet<QString>{ "rs:ndvi" };
    in.knownLabIds = QSet<QString>{ "lab90_r4" };
    in.labSpec = qjson( R"JSON({
      "spec_version": 1,
      "id": "lab90_r4",
      "steps": [
        { "id": "s1", "operator_id": "rs:not_registered", "params": { "x": 1 } }
      ]
    })JSON" );

    const TeachingReleaseReport report = runPreflight( in );
    CHECK_FALSE( report.ok );
    CHECK_FALSE( report.issues.isEmpty() );
    // The release-gate artifact must be reproducible across INDEPENDENT
    // runs: same input twice → identical canonical digest, so a teacher
    // cannot quietly re-roll a failed gate.
    const TeachingReleaseReport rerun = runPreflight( in );
    CHECK( report.canonicalDigest() == rerun.canonicalDigest() );
    CHECK( report.canonicalBytes() == rerun.canonicalBytes() );
    // And the gate actually binds to its input: perturbing the spec moves
    // the digest.
    in.labSpec.insert( QStringLiteral( "title" ), QStringLiteral( "t2" ) );
    CHECK( runPreflight( in ).canonicalDigest() != report.canonicalDigest() );
}

TEST_CASE( "authoring r4: operator catalog loads valid sidecars and types broken ones",
           "[teaching_r4][authoring][catalog]" )
{
    QTemporaryDir caps;
    writeBytes( caps.filePath( QStringLiteral( "rs-good.json" ) ),
                QByteArray( R"JSON({ "id": "rs:good", "parameters": { "x": { "type": "number" } } })JSON" ) );
    writeBytes( caps.filePath( QStringLiteral( "rs-broken.json" ) ),
                QByteArray( R"JSON({ "parameters": {} })JSON" ) ); // id-less

    const OperatorCatalog catalog = loadOperatorCatalog( caps.path() );
    CHECK( catalog.operatorIds.contains( QLatin1String( "rs:good" ) ) );
    CHECK_FALSE( catalog.operatorIds.contains( QLatin1String( "rs:broken" ) ) );
    REQUIRE_FALSE( catalog.issues.isEmpty() );
    CHECK_FALSE( catalog.issues.front().message.isEmpty() );
}

TEST_CASE( "fixture r4: a pack declaring a missing committed fixture is red at the inventory entry",
           "[teaching_r4][fixture][packs]" )
{
    QTemporaryDir root;
    const QDir packs = root.filePath( QStringLiteral( "packs" ) );
    QDir().mkpath( packs.path() );
    writeBytes( packs.filePath( QStringLiteral( "lab90_r4.pack.json" ) ),
                packJson( QStringLiteral( "data/labs/fixtures/absent_asset.tif" ), kAnySha ) );

    const PackInventory inventory = inventoryPacks( packs.path(), root.path() );
    REQUIRE( inventory.packs.size() == 1 );
    const PackInventoryEntry &entry = inventory.packs.front();

    CHECK_FALSE( entry.offlineAvailable );
    bool missingTyped = false;
    for ( const AdminIssue &issue : entry.issues )
    {
        if ( issue.code == QLatin1String( "input_missing" ) &&
             issue.severity == QLatin1String( "error" ) &&
             issue.message.contains( QLatin1String( "absent_asset.tif" ) ) )
            missingTyped = true;
    }
    CHECK( missingTyped ); // the rejection names the exact asset
}

TEST_CASE( "fixture r4: a pack whose committed fixture exists but drifted is red by digest",
           "[teaching_r4][fixture][packs]" )
{
    QTemporaryDir root;
    QDir().mkpath( root.filePath( QStringLiteral( "data/labs/fixtures" ) ) );
    writeBytes( root.filePath( QStringLiteral( "data/labs/fixtures/drifted.tif" ) ),
                QByteArray( "actual bytes on disk" ) );

    const QDir packs = root.filePath( QStringLiteral( "packs" ) );
    QDir().mkpath( packs.path() );
    writeBytes( packs.filePath( QStringLiteral( "lab90_r4.pack.json" ) ),
                packJson( QStringLiteral( "data/labs/fixtures/drifted.tif" ), kAnySha ) );

    const PackInventory inventory = inventoryPacks( packs.path(), root.path() );
    REQUIRE( inventory.packs.size() == 1 );

    bool digestTyped = false;
    for ( const AdminIssue &issue : inventory.packs.front().issues )
        if ( issue.code == QLatin1String( "digest_mismatch" ) &&
             issue.severity == QLatin1String( "error" ) )
            digestTyped = true;
    CHECK( digestTyped );
    CHECK_FALSE( inventory.packs.front().offlineAvailable );
}
