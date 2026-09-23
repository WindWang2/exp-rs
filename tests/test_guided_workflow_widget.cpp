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
#include <QSet>
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

    SECTION( "a second file cannot claim another file's id" )
    {
        // The loader enforces id == file stem, so two files in one directory
        // can never collide on id; a copy claiming another id is rejected by
        // the stem check.
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
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "does not match id" ) ) );
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

    // ADR 0166: the generated canonical LabSpec 2 documents (lab12–lab14,
    // steps owned by the pipeline) must be loadable by the strict loader —
    // they used to be refused ("steps must be a non-empty array"), pinning
    // three typed error entries on top of the widget's workflow list.
    QSet<QString> ids;
    for ( const LabSpec &spec : result.labs )
        ids.insert( spec.id );
    REQUIRE( ids.contains( QStringLiteral( "lab12_sar_processing" ) ) );
    REQUIRE( ids.contains( QStringLiteral( "lab13_hyperspectral_analysis" ) ) );
    REQUIRE( ids.contains( QStringLiteral( "lab14_cartographic_mapping" ) ) );
}

// ---------------------------------------------------------------------------
// Widget-level session-boundary oracles (hardening app-workbench-ui-shell).
//
// These drive the real GuidedWorkflowWidget offscreen: a lab directory under
// SICNU_DATA_DIR, rows selected through the real QListWidget, and the
// private slots invoked by name through the meta-object — the same calls the
// buttons' clicked() signals dispatch. They pin the cross-experiment
// boundary: selecting another workflow ends the active session. The stale
// step cursor used to leak into the newly selected workflow, so Next could
// declare a never-started workflow complete and Run indexed past its steps.
//
// These tests must stay declared AFTER the SICNU_DATA_DIR-free ones above:
// the override is process-global while a ScopedLabDir lives.
// ---------------------------------------------------------------------------

#include <QApplication>
#include <QListWidget>

namespace {

QApplication *ensureWidgetApp()
{
    if ( !QApplication::instance() )
    {
        static int argc = 1;
        static char argv0[] = "test_guided_workflow_widget";
        static char *argv[] = { argv0, nullptr };
        return new QApplication( argc, argv );
    }
    return static_cast<QApplication *>( QApplication::instance() );
}

/// Points SICNU_DATA_DIR at a temp root holding data/labs for the widget's
/// constructor-time loadWorkflows(); restores the previous value on scope
/// exit.
class ScopedLabDir
{
  public:
    ScopedLabDir()
        : m_previous( qgetenv( "SICNU_DATA_DIR" ) )
    {
        REQUIRE( m_dir.isValid() );
        qputenv( "SICNU_DATA_DIR", m_dir.path().toUtf8() );
    }
    ~ScopedLabDir() { qputenv( "SICNU_DATA_DIR", m_previous ); }

    QDir labsDir() const
    {
        const QDir root( m_dir.path() );
        REQUIRE( QDir().mkpath( root.filePath( QStringLiteral( "data/labs" ) ) ) );
        return QDir( root.filePath( QStringLiteral( "data/labs" ) ) );
    }

  private:
    QTemporaryDir m_dir;
    QByteArray m_previous;
};

/// 4-step lab whose step 1 is operator-bound (Run armed after one Next).
QString boundaryLabA()
{
    return QStringLiteral( R"( {
      "spec_version": 1,
      "id": "lab91_boundary_a",
      "title": "Boundary A",
      "title_zh": "边界A",
      "objective": "session boundary probe A",
      "steps": [
        { "title": "M0", "title_zh": "手动0", "description_zh": "手动步骤。" },
        { "title": "Op1", "title_zh": "算子1", "description_zh": "算子步骤。",
          "operator_id": "rs:spectral_index",
          "params": { "input": "data/samples/landsat_sample.tif", "output": "outputs/ndvi.tif", "index": "NDVI" } },
        { "title": "M2", "title_zh": "手动2", "description_zh": "手动步骤。" },
        { "title": "M3", "title_zh": "手动3", "description_zh": "手动步骤。" }
      ]
    } )" );
}

/// N-step all-manual lab with the fixed id lab92_boundary_b.
QString boundaryLabB( int steps )
{
    QString body = QStringLiteral( R"( {
      "spec_version": 1,
      "id": "lab92_boundary_b",
      "title": "Boundary B",
      "title_zh": "边界B",
      "objective": "session boundary probe B",
      "steps": [ )" );
    for ( int i = 0; i < steps; ++i )
    {
        body += QStringLiteral(
                    R"( { "title": "B%1", "title_zh": "B%1", "description_zh": "短实验。" } )" )
                    .arg( i );
        if ( i + 1 < steps )
            body += QLatin1String( ", " );
    }
    body += QLatin1String( " ] }" );
    return body;
}

} // namespace

TEST_CASE( "Selecting another workflow ends the active session", "[widget][workflow][boundary]" )
{
    ensureWidgetApp();
    ScopedLabDir labs;
    const QDir dir = labs.labsDir();
    writeFile( dir, QStringLiteral( "lab91_boundary_a.lab.json" ), boundaryLabA() );
    writeFile( dir, QStringLiteral( "lab92_boundary_b.lab.json" ), boundaryLabB( 2 ) );

    GuidedWorkflowWidget widget( nullptr );
    REQUIRE( widget.loadErrorStrings().isEmpty() );
    REQUIRE( widget.workflows().size() == 2 );

    auto *list = widget.findChild<QListWidget *>();
    REQUIRE( list != nullptr );

    int completedCount = 0;
    QObject::connect( &widget, &GuidedWorkflowWidget::workflowCompleted,
                      [&completedCount] { ++completedCount; } );

    // Start lab A (row 0) and advance once: active session on step 1.
    list->setCurrentRow( 0 );
    QMetaObject::invokeMethod( &widget, "onStartWorkflow" );
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    REQUIRE( completedCount == 0 );

    // Select lab B: the session boundary. The stale step cursor must not
    // carry into B — Next on the never-started B used to satisfy its
    // completion condition and declare the experiment complete.
    list->setCurrentRow( 1 );
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    REQUIRE( completedCount == 0 );
}

TEST_CASE( "Run This Step after switching workflows never indexes past the steps", "[widget][workflow][boundary]" )
{
    ensureWidgetApp();
    ScopedLabDir labs;
    const QDir dir = labs.labsDir();
    writeFile( dir, QStringLiteral( "lab91_boundary_a.lab.json" ), boundaryLabA() );
    writeFile( dir, QStringLiteral( "lab92_boundary_b.lab.json" ), boundaryLabB( 1 ) );

    GuidedWorkflowWidget widget( nullptr );
    REQUIRE( widget.loadErrorStrings().isEmpty() );
    auto *list = widget.findChild<QListWidget *>();
    REQUIRE( list != nullptr );

    // Lab A step 1 is operator-bound: the session is active with cursor 1.
    list->setCurrentRow( 0 );
    QMetaObject::invokeMethod( &widget, "onStartWorkflow" );
    QMetaObject::invokeMethod( &widget, "onNextStep" );

    // Switch to single-step lab B and press Run: the stale cursor must not
    // index B's steps (used to be a QList out-of-range read — Q_ASSERT abort
    // in debug builds, undefined behaviour in release).
    list->setCurrentRow( 1 );
    QMetaObject::invokeMethod( &widget, "onRunStepAction" );
    SUCCEED( "Run This Step stayed in bounds after the workflow switch" );
}

// ---------------------------------------------------------------------------
// Version-scoped steps contract (ADR 0146 / ADR 0166)
// ---------------------------------------------------------------------------

QString steplessV2Lab( const QString &id = QStringLiteral( "lab93_stepless_v2" ) )
{
    return QStringLiteral( R"( {
      "spec_version": 2,
      "id": "%1",
      "title": "Stepless V2",
      "title_zh": "无步骤V2",
      "objective": "Pipeline-owned operator sequence probe."
    } )" ).arg( id );
}

TEST_CASE( "The steps requirement is version-scoped", "[widget][workflow][labspec][versioning]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QDir dataDir( dir.filePath( QStringLiteral( "data/labs" ) ) );
    REQUIRE( QDir().mkpath( dataDir.absolutePath() ) );

    SECTION( "a v2 document without steps loads with an empty step list" )
    {
        writeFile( dataDir, QStringLiteral( "lab93_stepless_v2.lab.json" ), steplessV2Lab() );
        const LabLoadResult result = loadLabSpecsFromDir( dataDir.path() );
        INFO( errorStrings( result ).join( QStringLiteral( "; " ) ).toStdString() );
        REQUIRE( result.ok() );
        REQUIRE( result.labs.size() == 1 );
        REQUIRE( result.labs.first().stepCount() == 0 );
    }

    SECTION( "a v2 document with an empty steps array is still refused" )
    {
        const QString body = steplessV2Lab() .replace(
            QStringLiteral( "\"objective\": \"Pipeline-owned operator sequence probe.\"" ),
            QStringLiteral( "\"objective\": \"Pipeline-owned operator sequence probe.\", \"steps\": []" ) );
        writeFile( dataDir, QStringLiteral( "lab93_stepless_v2.lab.json" ), body );
        const LabLoadResult result = loadLabSpecsFromDir( dataDir.path() );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "steps must be a non-empty array" ) ) );
    }

    SECTION( "a v1 document still requires steps" )
    {
        writeFile( dataDir, QStringLiteral( "lab94_stepless_v1.lab.json" ), steplessV2Lab( QStringLiteral( "lab94_stepless_v1" ) )
                       .replace( QStringLiteral( "\"spec_version\": 2" ), QStringLiteral( "\"spec_version\": 1" ) ) );
        const LabLoadResult result = loadLabSpecsFromDir( dataDir.path() );
        REQUIRE( !result.ok() );
        REQUIRE( result.errors.first().reason.contains( QStringLiteral( "steps must be a non-empty array" ) ) );
    }
}

TEST_CASE( "A pipeline-owned (stepless) lab is listed honestly, not as an error",
           "[widget][workflow][boundary]" )
{
    ensureWidgetApp();
    ScopedLabDir labs;
    const QDir dir = labs.labsDir();
    writeFile( dir, QStringLiteral( "lab93_stepless_v2.lab.json" ), steplessV2Lab() );
    writeFile( dir, QStringLiteral( "lab91_boundary_a.lab.json" ), boundaryLabA() );

    GuidedWorkflowWidget widget( nullptr );
    REQUIRE( widget.loadErrorStrings().isEmpty() );
    REQUIRE( widget.workflows().size() == 2 );

    auto *list = widget.findChild<QListWidget *>();
    REQUIRE( list != nullptr );

    int startedCount = 0;
    QObject::connect( &widget, &GuidedWorkflowWidget::workflowStarted,
                      [&startedCount] { ++startedCount; } );

    // Select the stepless lab: Start is disabled (nothing to walk) and
    // invoking it directly must not start a ghost session.
    list->setCurrentRow( 1 );
    QMetaObject::invokeMethod( &widget, "onStartWorkflow" );
    REQUIRE( startedCount == 0 );
}

TEST_CASE( "Positive control: a walkable lab completes end to end",
           "[widget][workflow][boundary]" )
{
    // Guards against the inverse regression of the boundary oracles: an
    // over-tight guard that turns Start/Next into total no-ops would keep
    // every "nothing happened" assertion green. This walks lab A (4 steps)
    // through Start → three Next → terminal Next and requires the full
    // signal sequence: workflowStarted once, stepCompleted 0/1/2, then
    // workflowCompleted exactly once.
    ensureWidgetApp();
    ScopedLabDir labs;
    const QDir dir = labs.labsDir();
    writeFile( dir, QStringLiteral( "lab91_boundary_a.lab.json" ), boundaryLabA() );

    GuidedWorkflowWidget widget( nullptr );
    REQUIRE( widget.loadErrorStrings().isEmpty() );
    auto *list = widget.findChild<QListWidget *>();
    REQUIRE( list != nullptr );

    int startedCount = 0;
    int completedCount = 0;
    QVector<int> completedSteps;
    QObject::connect( &widget, &GuidedWorkflowWidget::workflowStarted,
                      [&startedCount] { ++startedCount; } );
    QObject::connect( &widget, &GuidedWorkflowWidget::workflowCompleted,
                      [&completedCount] { ++completedCount; } );
    QObject::connect( &widget, &GuidedWorkflowWidget::stepCompleted,
                      [&completedSteps]( int stepIndex ) { completedSteps << stepIndex; } );

    list->setCurrentRow( 0 );
    QMetaObject::invokeMethod( &widget, "onStartWorkflow" );
    REQUIRE( startedCount == 1 );
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    REQUIRE( completedCount == 0 ); // still on the last step, not done
    QMetaObject::invokeMethod( &widget, "onNextStep" );
    REQUIRE( completedCount == 1 );
    REQUIRE( completedSteps == QVector<int>{ 0, 1, 2 } );
}
