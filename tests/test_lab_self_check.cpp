/***************************************************************************
  tests/test_lab_self_check.cpp — `lab --self-check` (teaching-lab-11).

  Oracles:
    * determinism — the document carries no wall clock; two runs over the
      same state are byte-identical;
    * pack verification honesty — a corrupted committed fixture must flip the
      overall state to failed (the test corrupts a COPY of the source tree's
      fixture, never the tracked file);
    * rules parsing — every shipped rules file parses through the real
      grading seam (the evidence map carries per-lab state);
    * offline gate — the check reports what the process gate would refuse.
 ***************************************************************************/

#include "cli/lab_self_check.h"

#include "data/offline_mode.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

using sicnu::cli::LabSelfCheckOptions;
using sicnu::cli::runLabSelfCheck;

namespace
{

#ifdef SICNU_SOURCE_DIR
QString sourceRoot() { return QString::fromUtf8( SICNU_SOURCE_DIR ); }
#else
QString sourceRoot() { return QString(); }
#endif

QString documentToString( const Json::Value &document )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    return QString::fromStdString( Json::writeString( builder, document ) );
}

} // namespace

TEST_CASE( "self-check runs over the source tree without failures",
           "[lab_self_check][known_answer]" )
{
    if ( sourceRoot().isEmpty() )
        FAIL( "SICNU_SOURCE_DIR is required for this gate" );

    LabSelfCheckOptions options;
    options.packRoot = sourceRoot();
    bool ok = true;
    const Json::Value document = runLabSelfCheck( options, &ok );

    REQUIRE( document["schema"].asString() == "sicnu.lab.self-check/1" );
    // Pack state over a fresh source tree degrades (regenerable inputs absent
    // by design) but must never FAIL — a failure here means a committed
    // fixture is missing or corrupted in the tree.
    INFO( documentToString( document ).toStdString() );
    REQUIRE( document["overall"].asString() != "failed" );
    REQUIRE( ok );

    const Json::Value &checks = document["checks"];
    REQUIRE( checks.size() >= 4 );
    bool sawProj = false, sawGate = false, sawRules = false;
    for ( const auto &check : checks )
    {
        const std::string name = check["check"].asString();
        if ( name == "proj_authority" )
        {
            sawProj = true;
            REQUIRE( check["status"].asString() == "ok" );
        }
        else if ( name == "offline_gate" )
        {
            sawGate = true;
            REQUIRE( check["status"].asString() == "ok" );
        }
        else if ( name == "grading_rules" )
        {
            sawRules = true;
            REQUIRE( check["status"].asString() == "ok" );
            // The shipped rules set is non-empty and every entry parses.
            const Json::Value &evidence = check["evidence"];
            REQUIRE( evidence.size() >= 6 );
            for ( const auto &lab : evidence.getMemberNames() )
                REQUIRE( evidence[lab].asString() == "parsed" );
        }
    }
    REQUIRE( sawProj );
    REQUIRE( sawGate );
    REQUIRE( sawRules );
}

TEST_CASE( "self-check is deterministic (no wall clock in the document)",
           "[lab_self_check][determinism]" )
{
    LabSelfCheckOptions options;
    options.packRoot = sourceRoot();
    bool ok = false;
    const Json::Value first = runLabSelfCheck( options, &ok );
    const Json::Value second = runLabSelfCheck( options, &ok );
    REQUIRE( documentToString( first ) == documentToString( second ) );
}

TEST_CASE( "self-check flips to failed when a committed fixture is corrupted",
           "[lab_self_check][negative]" )
{
    if ( sourceRoot().isEmpty() )
        FAIL( "SICNU_SOURCE_DIR is required for this gate" );

    // A source-tree SHADOW: copy only the pack-relevant subtree into a temp
    // root, then corrupt one committed fixture there.
    QTemporaryDir shadow;
    REQUIRE( shadow.isValid() );
    const QString packsSrc = sourceRoot() + "/data/labs/packs";
    const QString packsDst = shadow.filePath( "data/labs/packs" );
    REQUIRE( QDir().mkpath( packsDst ) );
    for ( const QFileInfo &entry : QDir( packsSrc ).entryInfoList( QStringList() << "*.pack.json" ) )
        REQUIRE( QFile::copy( entry.absoluteFilePath(), packsDst + "/" + entry.fileName() ) );

    // Only the grading corpus pack is needed to make the corruption visible;
    // point the options at a reduced rules dir skip? No: corrupt ONE fixture.
    const QString fixturesDst = shadow.filePath( "tests/fixtures/lab" );
    REQUIRE( QDir().mkpath( fixturesDst ) );
    QByteArray corrupt;
    {
        QFile reference( sourceRoot() + "/tests/fixtures/lab/terrain_slope_reference.tif" );
        REQUIRE( reference.open( QIODevice::ReadOnly ) );
        corrupt = reference.readAll();
    }
    if ( !corrupt.isEmpty() )
        corrupt[0] = static_cast<char>( corrupt[0] ^ 0xFF );
    QFile shadowFixture( fixturesDst + "/terrain_slope_reference.tif" );
    REQUIRE( shadowFixture.open( QIODevice::WriteOnly ) );
    REQUIRE( shadowFixture.write( corrupt ) == corrupt.size() );
    shadowFixture.close();

    // The full corpus must be present in the shadow for honest verification:
    // copy the remaining committed fixtures untouched.
    QDir( sourceRoot() + "/tests/fixtures/lab" )
      .entryList( QStringList() << "*.tif" << "*.json", QDir::Files );
    for ( const QFileInfo &entry :
          QDir( sourceRoot() + "/tests/fixtures/lab" )
            .entryInfoList( QStringList() << "*.tif" << "*.json", QDir::Files ) )
    {
        const QString target = fixturesDst + "/" + entry.fileName();
        if ( !QFile::exists( target ) )
            REQUIRE( QFile::copy( entry.absoluteFilePath(), target ) );
    }

    LabSelfCheckOptions options;
    options.packRoot = shadow.path();
    bool ok = true;
    const Json::Value document = runLabSelfCheck( options, &ok );

    INFO( documentToString( document ).toStdString() );
    REQUIRE( document["overall"].asString() == "failed" );
    REQUIRE( ok == false );
    // The evidence names the corrupted pack.
    Json::Value packs;
    for ( const auto &check : document["checks"] )
        if ( check["check"].asString() == "lab_data_packs" )
            packs = check["evidence"]["packs"];
    REQUIRE( packs.isObject() );
    bool sawCorpusFailed = false;
    for ( const auto &name : packs.getMemberNames() )
        if ( name == "grading_corpus" )
            sawCorpusFailed = packs[name].asString() == "failed";
    REQUIRE( sawCorpusFailed );
}

TEST_CASE( "self-check offline gate check mirrors the process gate",
           "[lab_self_check][offline]" )
{
    LabSelfCheckOptions options;
    options.packRoot = QString(); // skip pack walk (focus on the gate)
#ifdef SICNU_SOURCE_DIR
    options.rulesDir = sourceRoot() + "/data/labs/grading";
#endif

    sicnu::data::offline::setEnabled( true );
    bool ok = false;
    const Json::Value document = runLabSelfCheck( options, &ok );
    sicnu::data::offline::setEnabled( false );

    bool sawGate = false;
    for ( const auto &check : document["checks"] )
    {
        if ( check["check"].asString() == "offline_gate" )
        {
            sawGate = true;
            REQUIRE( check["evidence"]["engaged"].asBool() == true );
            REQUIRE( check["evidence"]["remote_target_refused"].asBool() == true );
        }
    }
    REQUIRE( sawGate );
}
