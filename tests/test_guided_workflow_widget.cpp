// test_guided_workflow_widget.cpp — GuidedWorkflowWidget behavioural tests
//
// The widget renders whatever lab::loadLabSpecsFromDir returns; these tests
// pin the data layer it consumes (the widget itself only adds Qt chrome on
// top): real labs load, step counts are real, invalid specs are refused with
// typed errors, and param path resolution follows the documented contract.
#include <catch2/catch_test_macros.hpp>

#include <app/widgets/guided_workflow_widget.h>
#include <app/widgets/lab_spec_loader.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>

using namespace lab;

namespace {

/// Writes `content` into <dir>/<name> and returns the full path.
QString writeFile( const QDir &dir, const QString &name, const QString &content )
{
    const QString path = dir.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
    file.write( content.toUtf8() );
    return path;
}

/// A minimal valid lab body; `overrides` patches the JSON before writing.
QString validLabJson( const QString &id = QStringLiteral( "lab99_probe_lab" ) )
{
    return QStringLiteral( R"( {
      "spec_version": 1,
      "id": "%1",
      "title": "Probe Lab",
      "title_zh": "探测实验",
      "objective": "Probe the loader.",
      "steps": [
        { "title": "Manual", "title_zh": "手动", "description_zh": "什么也不做。" },
        { "title": "Compute", "title_zh": "计算", "description_zh": "算一下。",
          "operator_id": "rs:spectral_index",
          "params": { "input": "data/samples/landsat_sample.tif", "output": "outputs/ndvi.tif", "index": "NDVI" } },
        { "title": "Open", "title_zh": "打开", "description_zh": "打开图层。",
          "action": "addRasterLayer" }
      ]
    } )" ).arg( id );
}

LabLoadResult loadDir( const QTemporaryDir &dir )
{
    return loadLabSpecsFromDir( dir.path() );
}

} // namespace

TEST_CASE( "WorkflowStep models operator-bound steps", "[widget][workflow]" )
{
    SECTION( "Manual / action / operator classification" )
    {
        WorkflowStep manual;
        REQUIRE( manual.isManual() );
        REQUIRE( !manual.hasOperator() );

        WorkflowStep uiVerb;
        uiVerb.action = QStringLiteral( "addRasterLayer" );
        REQUIRE( uiVerb.isManual() == false );
        REQUIRE( !uiVerb.hasOperator() );

        WorkflowStep op;
        op.operatorId = QStringLiteral( "rs:spectral_index" );
        op.params = Json::Value( Json::objectValue );
        REQUIRE( op.hasOperator() );
    }
}

TEST_CASE( "Valid LabSpecs load from a directory", "[widget][workflow][labspec]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QDir qdir( dir.path() );
    writeFile( qdir, QStringLiteral( "lab99_probe_lab.lab.json" ), validLabJson() );

    const LabLoadResult result = loadDir( dir );
    REQUIRE( result.ok() );
    REQUIRE( result.labs.size() == 1 );

    const LabSpec &spec = result.labs.first();
    REQUIRE( spec.id == QStringLiteral( "lab99_probe_lab" ) );
    REQUIRE( spec.titleZh == QStringLiteral( "探测实验" ) );
    REQUIRE( spec.stepCount() == 3 );
    REQUIRE( spec.steps[0].isManual() );
    REQUIRE( spec.steps[1].operatorId == QStringLiteral( "rs:spectral_index" ) );
    REQUIRE( spec.steps[1].params[ "index" ].asString() == "NDVI" );
    REQUIRE( spec.steps[2].action == QStringLiteral( "addRasterLayer" ) );
}

TEST_CASE( "Invalid LabSpecs are refused with typed errors", "[widget][workflow][labspec]" )
{
    struct Case { QString name; QString json; QString reasonPart; };
    const QList<Case> cases = {
        { QStringLiteral( "unknown_key" ),
          validLabJson().replace( QStringLiteral( "\"objective\":" ),
                                  QStringLiteral( "\"objectivex\": null, \"objective\":" ) ),
          QStringLiteral( "unknown top-level key" ) },
        { QStringLiteral( "bad_json" ), QStringLiteral( "{ not json }" ), QStringLiteral( "invalid JSON" ) },
        { QStringLiteral( "empty_steps" ),
          QStringLiteral( R"( { "spec_version": 1, "id": "lab99_probe_lab", "title": "t",
                              "title_zh": "t", "objective": "o", "steps": [] } )" ),
          QStringLiteral( "steps must be a non-empty array" ) },
    };

    for ( const auto &testCase : cases )
    {
        SECTION( testCase.name.toStdString() )
        {
            QTemporaryDir dir;
            REQUIRE( dir.isValid() );
            writeFile( QDir( dir.path() ), QStringLiteral( "lab99_probe_lab.lab.json" ), testCase.json );
            const LabLoadResult result = loadDir( dir );
            REQUIRE( !result.ok() );
            REQUIRE( result.labs.isEmpty() );
            REQUIRE( result.errors.first().reason.contains( testCase.reasonPart ) );
        }
    }

    SECTION( "operator_id and action are mutually exclusive" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        writeFile( QDir( dir.path() ), QStringLiteral( "lab99_probe_lab.lab.json" ),
                   validLabJson().replace( QStringLiteral( "\"action\": \"addRasterLayer\"" ),
                                           QStringLiteral( "\"operator_id\": \"rs:pca\", \"action\": \"addRasterLayer\"" ) ) );
        const LabLoadResult result = loadDir( dir );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "mutually exclusive" ) ) );
    }

    SECTION( "params without operator_id is rejected" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        writeFile( QDir( dir.path() ), QStringLiteral( "lab99_probe_lab.lab.json" ),
                   validLabJson().replace( QStringLiteral( "\"action\": \"addRasterLayer\"" ),
                                           QStringLiteral( "\"action\": \"addRasterLayer\", \"params\": {}" ) ) );
        const LabLoadResult result = loadDir( dir );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "params requires operator_id" ) ) );
    }

    SECTION( "file stem must match the id" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        writeFile( QDir( dir.path() ), QStringLiteral( "lab98_wrong_name.lab.json" ), validLabJson() );
        const LabLoadResult result = loadDir( dir );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "does not match id" ) ) );
    }

    SECTION( "duplicate ids across files are rejected" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const QDir qdir( dir.path() );
        writeFile( qdir, QStringLiteral( "lab99_probe_lab.lab.json" ), validLabJson() );
        writeFile( qdir, QStringLiteral( "lab99_probe_lab_copy.lab.json" ),
                   validLabJson( QStringLiteral( "lab99_probe_lab_copy" ) )
                       .replace( QStringLiteral( "\"id\": \"lab99_probe_lab_copy\"" ),
                                 QStringLiteral( "\"id\": \"lab99_probe_lab\"" ) ) );
        const LabLoadResult result = loadDir( dir );
        REQUIRE( !result.ok() );
        REQUIRE( result.labs.size() == 1 );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "duplicate lab id" ) ) );
    }

    SECTION( "missing directory is a typed error" )
    {
        const LabLoadResult result = loadLabSpecsFromDir( QStringLiteral( "/nonexistent/labs/dir" ) );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "does not exist" ) ) );
    }
}

TEST_CASE( "Lab param paths resolve through the injected resolver", "[widget][workflow][labspec]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    writeFile( QDir( dir.path() ), QStringLiteral( "lab99_probe_lab.lab.json" ), validLabJson() );
    const LabSpec spec = loadDir( dir ).labs.first();
    const Json::Value params = spec.steps[1].params;

    QString capturedInput;
    QString capturedOutput;
    const Json::Value resolved = resolveLabParamPaths(
        params, spec.id,
        [&]( const QString &relative, PathRole role )
        {
            if ( role == PathRole::Input )
            {
                capturedInput = relative;
                return QStringLiteral( "/runtime/root/%1" ).arg( relative );
            }
            capturedOutput = relative;
            return QStringLiteral( "/runtime/%1" ).arg( relative );
        } );

    REQUIRE( capturedInput == QStringLiteral( "data/samples/landsat_sample.tif" ) );
    REQUIRE( resolved[ "input" ].asString() == "/runtime/root/data/samples/landsat_sample.tif" );
    REQUIRE( capturedOutput == QStringLiteral( "output/labs/lab99_probe_lab/ndvi.tif" ) );
    REQUIRE( resolved[ "output" ].asString() == "/runtime/output/labs/lab99_probe_lab/ndvi.tif" );
    // Non-path values pass through untouched.
    REQUIRE( resolved[ "index" ].asString() == "NDVI" );
}

TEST_CASE( "Shipped labs load through the same path the widget uses", "[widget][workflow][labspec]" )
{
    const LabLoadResult result = loadLabSpecsFromDir( defaultLabDirectory() );
    if ( result.errors.size() == 1 && result.errors.first().reason.contains( QStringLiteral( "does not exist" ) ) )
    {
        // Running outside a source checkout (e.g. installed package without
        // SICNU_DATA_DIR): nothing to verify here — skip.
        return;
    }
    INFO( errorStrings( result ).join( QStringLiteral( "; " ) ).toStdString() );
    REQUIRE( result.ok() );
    REQUIRE( result.labs.size() >= 10 );
}
