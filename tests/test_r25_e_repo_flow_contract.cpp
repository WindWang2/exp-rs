/***************************************************************************
  tests/test_r25_e_repo_flow_contract.cpp — R25-E (CT-D1): the repo-side
  student flow script must grade the artifact it actually produces.

  scripts/windows/run_lab.cmd steps 1-2 generate and process the FOUNDRY
  scene (256x256 EPSG:32648, seed 42, via the lab1 pipeline). Its grading
  step used the FIXTURE authority (`--lab ndvi_basics`, derived from the
  committed 128x128 EPSG:4326 scene), so the scripted student flow failed
  with a measured 45/100 on master — two scenes, two authorities, and the
  script pointed at the wrong one (R21-E CT-6, R23-D CT-D1, handed to the
  E track).

  This contract test pins the script↔rules coherence the way the runtime
  cannot: the repo flow grades foundry output with the foundry authority
  (ndvi_landsat), that authority exists with the foundry grid pins, and it
  stays parameter-identical to the offline-bundle overlay it mirrors (the
  r23-D calibration contract: same authority, one copy per rules root).
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>

#include <QString>
#include <QStringList>

namespace
{

QString repoFile( const QString &relative )
{
    const QString path = QStringLiteral( SICNU_SOURCE_DIR ) + QLatin1Char( '/' ) + relative;
    REQUIRE( QFile::exists( path ) );
    return path;
}

QByteArray readFile( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return f.readAll();
}

QJsonObject readJson( const QString &path )
{
    const QByteArray bytes = readFile( path );
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson( bytes, &error );
    REQUIRE( error.error == QJsonParseError::NoError );
    REQUIRE( doc.isObject() );
    return doc.object();
}

} // namespace

TEST_CASE( "run_lab.cmd grades its foundry artifact with the foundry authority",
           "[r25_e][ct_d1][repo_flow]" )
{
    const QString scriptPath =
      repoFile( QStringLiteral( "scripts/windows/run_lab.cmd" ) );
    const QString script = QString::fromUtf8( readFile( scriptPath ) );

    // The script still drives the foundry pipeline (steps 1-2)…
    REQUIRE( script.contains( QStringLiteral( "lab1_ndvi.pipeline.json" ) ) );
    // …so its grading step must address the foundry authority. The fixture
    // authority (`--lab ndvi_basics`) is the CT-D1 defect: it measured
    // 45/100 against this very artifact on master.
    REQUIRE( script.contains( QStringLiteral( "--lab ndvi_landsat" ) ) );
    REQUIRE( !script.contains( QStringLiteral( "--lab ndvi_basics" ) ) );
}

TEST_CASE( "the foundry authority exists with the foundry grid pins and mirrors the bundle overlay",
           "[r25_e][ct_d1][repo_flow]" )
{
    const QString repoRulesPath =
      repoFile( QStringLiteral( "data/labs/grading/ndvi_landsat.rules.json" ) );
    const QJsonObject repoRules = readJson( repoRulesPath );
    REQUIRE( repoRules.value( QStringLiteral( "lab_id" ) ).toString() ==
             QStringLiteral( "ndvi_landsat" ) );
    REQUIRE( repoRules.value( QStringLiteral( "schema_version" ) ).toString() ==
             QStringLiteral( "sicnu.lab.rules/1" ) );

    // Foundry grid contract (ADR 0164): 256x256 at 30 m in EPSG:32648. If
    // this file ever drifts toward the fixture scene (128x128 EPSG:4326),
    // the foundry flow grades against the wrong known answer.
    const QJsonArray assertions = repoRules.value( QStringLiteral( "assertions" ) ).toArray();
    REQUIRE( assertions.size() == 5 );
    bool sawFoundryGrid = false;
    for ( const QJsonValue &value : assertions )
    {
        const QJsonObject assertion = value.toObject();
        if ( assertion.value( QStringLiteral( "kind" ) ).toString() != QLatin1String( "crs_grid" ) )
            continue;
        const QJsonObject params = assertion.value( QStringLiteral( "params" ) ).toObject();
        REQUIRE( params.value( QStringLiteral( "epsg" ) ).toInt() == 32648 );
        REQUIRE( params.value( QStringLiteral( "width" ) ).toInt() == 256 );
        REQUIRE( params.value( QStringLiteral( "height" ) ).toInt() == 256 );
        sawFoundryGrid = true;
    }
    REQUIRE( sawFoundryGrid );

    // The offline-classroom overlay is the SAME authority under its bundle
    // id (ndvi_basics inside the bundle's own rules root). The r23-D
    // calibration contract pins the two files parameter-identical: every
    // assertion's params must match, or the classroom and repo flows grade
    // the same artifact differently.
    const QJsonObject overlayRules = readJson(
      repoFile( QStringLiteral( "packaging/bundle/labs/grading-overlay/ndvi_basics.rules.json" ) ) );
    const QJsonArray overlayAssertions =
      overlayRules.value( QStringLiteral( "assertions" ) ).toArray();
    REQUIRE( overlayAssertions.size() == assertions.size() );

    auto indexById = []( const QJsonArray &array ) {
        QHash<QString, QJsonObject> byId;
        for ( const QJsonValue &value : array )
        {
            const QJsonObject assertion = value.toObject();
            byId.insert( assertion.value( QStringLiteral( "id" ) ).toString(), assertion );
        }
        return byId;
    };
    const QHash<QString, QJsonObject> repo = indexById( assertions );
    const QHash<QString, QJsonObject> overlay = indexById( overlayAssertions );
    REQUIRE( repo.size() == overlay.size() );
    for ( auto it = repo.constBegin(); it != repo.constEnd(); ++it )
    {
        INFO( "assertion id: " << it.key().toUtf8().constData() );
        REQUIRE( overlay.contains( it.key() ) );
        REQUIRE( it.value().value( QStringLiteral( "params" ) ).toObject() ==
                 overlay.value( it.key() ).value( QStringLiteral( "params" ) ).toObject() );
        REQUIRE( it.value().value( QStringLiteral( "weight" ) ).toInt() ==
                 overlay.value( it.key() ).value( QStringLiteral( "weight" ) ).toInt() );
        REQUIRE( it.value().value( QStringLiteral( "severity" ) ).toString() ==
                 overlay.value( it.key() ).value( QStringLiteral( "severity" ) ).toString() );
    }
}
