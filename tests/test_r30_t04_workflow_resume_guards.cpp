// tests/test_r30_t04_workflow_resume_guards.cpp — R30-T04 WP07 (D4).
//
// Resume-safety guards for still-valid swarm findings (#1955):
//   G1 (P2-3) a checkpoint whose `attempt` is out of the sane range
//      (corrupted / hostile document) is REFUSED with a typed error — the
//      old `+ 1` on an unvalidated int64 overflowed (UB) and narrowed on
//      assignment to the 32-bit run-state counter.
//   G2 (P2-2) a fresh run started with a RELATIVE run-directory spelling
//      still anchors its checkpoint INSIDE that directory's absolute
//      location (no CWD-relative checkpoint that a cwd change or a peer
//      process could not find back), and the recorded run directory is
//      absolute so the resume path works from any cwd.
#include <catch2/catch_test_macros.hpp>
#include "support/pipeline_wait.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "workflow/pipeline_run_coordinator.h"

using namespace sicnu::workflow;

namespace
{

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fake_argc = 1;
        static char fake_argv[] = "test_r30_t04_workflow_resume_guards";
        static char *fake_argv_ptr[] = { fake_argv };
        app = new QApplication( fake_argc, fake_argv_ptr );
    }
    return app;
}

NodeFact chainNode( const QString &id, const QString &op )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = op;
    n.displayName = id;
    n.canvasPosition = QPointF( 0, 0 );
    n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } };
    n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ),
                               QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } };
    return n;
}

WorkflowDocument chain( int steps )
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-r30t04-%1" ).arg( steps );
    for ( int i = 1; i <= steps; ++i )
    {
        NodeFact node = chainNode( QStringLiteral( "node_%1" ).arg( i ), QStringLiteral( "rs:step" ) );
        if ( i == 1 )
            node.inputPorts.clear();
        def.nodes.append( node );
        if ( i > 1 )
            def.edges.append( EdgeFact{ QStringLiteral( "e%1" ).arg( i ),
                                         QStringLiteral( "node_%1" ).arg( i - 1 ),
                                         QStringLiteral( "output" ),
                                         QStringLiteral( "node_%1" ).arg( i ),
                                         QStringLiteral( "input" ) } );
    }
    return def;
}

} // namespace

TEST_CASE( "G1: an out-of-range checkpoint attempt is refused typed (swarm #1955 P2-3)",
           "[r30][t04][workflow_resume_guards]" )
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    QString checkpointFile;
    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        REQUIRE( coordinator.startRun( chain( 2 ), tmp.filePath( "run" ) ) );
        REQUIRE( sicnu_test::waitForPipelineCompleted( coordinator, 20000 ) );
        checkpointFile = coordinator.checkpointPath();
    }
    REQUIRE( QFileInfo::exists( checkpointFile ) );

    // Corrupt the attempt counter beyond the sane bound (and, for the second
    // probe, near INT64_MAX where the old `+ 1` was signed-overflow UB).
    for ( const qint64 hostile : { qint64( 1000001 ), qint64( 9223372036854775807 ) } )
    {
        QFile f( checkpointFile );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        QJsonObject doc = QJsonDocument::fromJson( f.readAll() ).object();
        f.close();
        doc.insert( QStringLiteral( "attempt" ), hostile );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( QJsonDocument( doc ).toJson() );
        f.close();

        PipelineRunCoordinator resumer;
        resumer.setExecutor( makeSyntheticNodeExecutor() );
        QString error;
        REQUIRE_FALSE( resumer.resumeFromCheckpoint( checkpointFile, &error ) );
        CAPTURE( hostile, error.toStdString() );
        CHECK( error.contains( QStringLiteral( "out-of-range resume attempt" ) ) );
    }

    // A legitimate attempt value still resumes (attempt 1 → attempt 2).
    {
        QFile f( checkpointFile );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        QJsonObject doc = QJsonDocument::fromJson( f.readAll() ).object();
        f.close();
        doc.insert( QStringLiteral( "attempt" ), 1 );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( QJsonDocument( doc ).toJson() );
        f.close();

        PipelineRunCoordinator resumer;
        resumer.setExecutor( makeSyntheticNodeExecutor() );
        QString error;
        CAPTURE( error.toStdString() );
        REQUIRE( resumer.resumeFromCheckpoint( checkpointFile, &error ) );
        REQUIRE( sicnu_test::waitForPipelineCompleted( resumer, 20000 ) );
        CHECK( resumer.provenancePath().contains( QStringLiteral( "attempt-2" ) ) );
    }
}

TEST_CASE( "G2: a relative run-directory spelling still anchors the checkpoint absolutely (swarm #1955 P2-2)",
           "[r30][t04][workflow_resume_guards]" )
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString absRunDir = tmp.filePath( "run" );
    // A RELATIVE spelling of the same directory (relative to the current
    // working directory — exactly what a caller with a different cwd base
    // would hand over).
    const QString relRunDir = QDir( QDir::currentPath() ).relativeFilePath( absRunDir );
    REQUIRE( QDir::isRelativePath( relRunDir ) );

    QString checkpointFile;
    {
        PipelineRunCoordinator coordinator;
        coordinator.setExecutor( makeSyntheticNodeExecutor() );
        QString error;
        REQUIRE( coordinator.startRun( chain( 2 ), relRunDir, &error ) );
        REQUIRE( sicnu_test::waitForPipelineCompleted( coordinator, 20000 ) );
        checkpointFile = coordinator.checkpointPath();
    }

    // The checkpoint landed INSIDE the intended run directory — an absolute
    // path under the temp dir, not a CWD-relative artifact.
    CHECK( QDir::isAbsolutePath( checkpointFile ) );
    CHECK( QFileInfo( checkpointFile ).absolutePath() == absRunDir );
    CHECK( QDir( absRunDir ).entryList( { QStringLiteral( "checkpoint_*.json" ) } ).size() == 1 );

    // And the recorded run directory is absolute, so a resume from a
    // DIFFERENT working directory finds the artifacts back.
    {
        PipelineRunCoordinator resumer;
        resumer.setExecutor( makeSyntheticNodeExecutor() );
        QString error;
        CAPTURE( error.toStdString() );
        REQUIRE( resumer.resumeFromCheckpoint( checkpointFile, &error ) );
        REQUIRE( sicnu_test::waitForPipelineCompleted( resumer, 20000 ) );
    }
}
