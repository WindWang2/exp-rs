#include <catch2/catch_test_macros.hpp>

#include "support/pipeline_wait.h"
#include "workflow/pipeline_run_coordinator.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QTemporaryDir>

using namespace sicnu::workflow;

namespace
{

QApplication *ensureApp()
{
  if ( QApplication::instance() )
    return qobject_cast<QApplication *>( QApplication::instance() );
  static int argc = 1;
  static char name[] = "test_r31_t01_workflow_resume";
  static char *argv[] = { name, nullptr };
  return new QApplication( argc, argv );
}

struct CurrentDirectoryGuard
{
    const QString original = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent( original ); }
};

NodeFact node( const QString &id )
{
  NodeFact fact;
  fact.nodeId = id;
  fact.operatorId = QStringLiteral( "rs:step" );
  fact.displayName = id;
  fact.canvasPosition = QPointF( 0, 0 );
  fact.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ),
                                 QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, false } };
  fact.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ),
                                QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 1, true } };
  return fact;
}

WorkflowDocument chain()
{
  WorkflowDocument def;
  def.workflowId = QStringLiteral( "wf-r31-resume" );
  def.nodes = { node( QStringLiteral( "first" ) ), node( QStringLiteral( "second" ) ) };
  def.nodes[0].inputPorts.clear();
  def.edges = { EdgeFact{ QStringLiteral( "edge" ), QStringLiteral( "first" ),
                          QStringLiteral( "output" ), QStringLiteral( "second" ),
                          QStringLiteral( "input" ) } };
  return def;
}

QJsonObject readDocument( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  const QJsonDocument document = QJsonDocument::fromJson( file.readAll() );
  REQUIRE( document.isObject() );
  return document.object();
}

void writeDocument( const QString &path, const QJsonObject &object )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  REQUIRE( file.write( QJsonDocument( object ).toJson() ) > 0 );
}

} // namespace

TEST_CASE( "R31-T01 WP05: a relative resume checkpoint keeps its identity across CWD changes",
           "[r31][t01][workflow_resume]" )
{
  ensureApp();
  CurrentDirectoryGuard restoreCwd;
  QTemporaryDir root;
  REQUIRE( root.isValid() );

  QString checkpoint;
  {
    PipelineRunCoordinator initial;
    initial.setExecutor( makeSyntheticNodeExecutor() );
    REQUIRE( initial.startRun( chain(), root.filePath( QStringLiteral( "run" ) ) ) );
    REQUIRE( sicnu_test::waitForPipelineCompleted( initial, 20000 ) );
    checkpoint = initial.checkpointPath();
  }
  REQUIRE( QFileInfo::exists( checkpoint ) );

  QJsonObject document = readDocument( checkpoint );
  QJsonArray nodes = document.value( QStringLiteral( "nodes" ) ).toArray();
  REQUIRE( nodes.size() == 2 );
  QJsonObject second = nodes[1].toObject();
  second.insert( QStringLiteral( "state" ), QStringLiteral( "Pending" ) );
  second.insert( QStringLiteral( "artifact" ), QString() );
  second.insert( QStringLiteral( "artifactFingerprint" ), QString() );
  second.insert( QStringLiteral( "artifactSize" ), -1 );
  second.insert( QStringLiteral( "artifactMtimeMs" ), -1 );
  second.insert( QStringLiteral( "isCacheHit" ), false );
  nodes[1] = second;
  document.insert( QStringLiteral( "nodes" ), nodes );
  document.insert( QStringLiteral( "finished" ), false );
  document.insert( QStringLiteral( "success" ), false );
  writeDocument( checkpoint, document );

  REQUIRE( QDir::setCurrent( QFileInfo( checkpoint ).absolutePath() ) );
  const QString relative = QFileInfo( checkpoint ).fileName();
  QTemporaryDir other;
  REQUIRE( other.isValid() );
  const QString misplaced = other.filePath( relative );

  PipelineRunCoordinator resumed;
  QSemaphore started;
  QSemaphore release;
  auto synthetic = makeSyntheticNodeExecutor();
  resumed.setExecutor( [&started, &release, synthetic](
                           const NodeFact &fact, const QHash<QString, QString> &inputs,
                           const QString &runDirectory, const std::atomic<bool> *cancel ) {
    started.release();
    release.acquire();
    return synthetic( fact, inputs, runDirectory, cancel );
  } );
  QString error;
  REQUIRE( resumed.resumeFromCheckpoint( relative, &error ) );
  REQUIRE( started.tryAcquire( 1, 30000 ) );
  REQUIRE( QDir::setCurrent( other.path() ) );
  release.release();
  REQUIRE( sicnu_test::waitForPipelineCompleted( resumed, 20000 ) );

  CHECK( resumed.checkpointPath() == checkpoint );
  CHECK( QFileInfo::exists( checkpoint ) );
  CHECK_FALSE( QFileInfo::exists( misplaced ) );
  const QJsonObject terminal = readDocument( checkpoint );
  CHECK( terminal.value( QStringLiteral( "attempt" ) ).toInt() == 2 );
  CHECK( terminal.value( QStringLiteral( "finished" ) ).toBool() );
  CHECK( terminal.value( QStringLiteral( "success" ) ).toBool() );
  CHECK( resumed.provenancePath().startsWith( root.filePath( QStringLiteral( "run" ) ) ) );
  CHECK( resumed.provenancePath().contains( QStringLiteral( "attempt-2" ) ) );
}
