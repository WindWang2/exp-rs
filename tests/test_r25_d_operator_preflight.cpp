// tests/test_r25_d_operator_preflight.cpp — R25-D Loop 2
//
// Oracle 2a (pre-run preflight/guidance chain): the task panel's dead
// "estimate" seam (TaskPanelHost::setEstimate — zero callers repo-wide)
// becomes a live preflight line derived from the AUTHORITATIVE
// AlgorithmMetaStore sidecars (the same knowledge agents select algorithms
// with), wired at both showTool sites of WorkflowSessionController.
//
// This suite pins the pure derivation; the production wiring is pinned by
// a source scan (the controller is assembled with the full shell).
#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "app/shell/operator_preflight.h"

#include "processing/framework/algorithm_meta_store.h"

#include <QDir>
#include <json/json.h>

#ifndef SICNU_TEST_DATA_ROOT
#define SICNU_TEST_DATA_ROOT CMAKE_SOURCE_DIR
#endif

using sicnu::app::operatorPreflightSummary;
using sicnu::processing::AlgorithmMetaStore;

TEST_CASE( "operatorPreflightSummary derives from the authoritative sidecar", "[r25d][preflight]" )
{
  const size_t loaded = AlgorithmMetaStore::instance().loadFromDirectory(
    QDir( QStringLiteral( SICNU_TEST_DATA_ROOT ) ).filePath( QStringLiteral( "data/processing/algorithm_meta" ) ).toStdString() );
  REQUIRE( loaded >= 10 );

  SECTION( "rs:change — declared GPU inference, raster contract" )
  {
    const QString summary = operatorPreflightSummary( "rs:change" );
    INFO( summary.toStdString() );
    CHECK_FALSE( summary.isEmpty() );
    CHECK( summary.contains( QStringLiteral( "raster" ) ) );
    CHECK( summary.contains( QStringLiteral( "GPU" ) ) );
    // The sidecar's notes are selection guidance the user never saw before.
    CHECK( summary.contains( QStringLiteral( "co-registered" ) ) );
  }

  SECTION( "unknown operator — honest absence, no invented facts" )
  {
    CHECK( operatorPreflightSummary( "rs:no_such_operator" ).isEmpty() );
  }

  SECTION( "gpu undeclared — never claims CPU-only (#707 drift rule)" )
  {
    // Find a sidecar without a gpu declaration and assert the summary makes
    // no GPU claim in either direction.
    bool sawUndeclared = false;
    for ( const auto &entry : AlgorithmMetaStore::instance().entries() )
    {
      if ( entry.gpuDeclared )
        continue;
      sawUndeclared = true;
      const QString summary = operatorPreflightSummary( entry.id );
      INFO( entry.id << " → " << summary.toStdString() );
      CHECK_FALSE( summary.contains( QStringLiteral( "GPU" ) ) );
      CHECK_FALSE( summary.contains( QStringLiteral( "CPU" ) ) );
    }
    CHECK( sawUndeclared ); // the guard is exercised by real data
  }

  SECTION( "summary is bounded — notes are truncated, never unbounded" )
  {
    for ( const auto &entry : AlgorithmMetaStore::instance().entries() )
    {
      const QString summary = operatorPreflightSummary( entry.id );
      CHECK( summary.size() <= 400 );
    }
  }
}

TEST_CASE( "WorkflowSessionController wires the preflight line at both tool surfaces", "[r25d][preflight][source_scan]" )
{
  QFile file( QStringLiteral( CMAKE_SOURCE_DIR ) + QStringLiteral( "/src/app/shell/workflow_session_controller.cpp" ) );
  REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString source = QTextStream( &file ).readAll();

  // The single-tool surface (openTool) and the node-selection surface
  // (selectStep) BOTH must project the sidecar-derived line into the panel
  // — one knowledge source, no per-site re-derivation.
  const auto blockContains = [&source]( const QString &anchor, const QString &needle ) {
    const int pos = source.indexOf( anchor );
    if ( pos < 0 )
      return false;
    const int nextShowTool = source.indexOf( QStringLiteral( "m_panel->showTool(" ), pos + 1 );
    const int end = nextShowTool > 0 ? nextShowTool : source.size();
    return source.mid( pos, end - pos ).contains( needle );
  };

  // openTool site
  CHECK( blockContains( QStringLiteral( "QString WorkflowSessionController::openTool" ),
                        QStringLiteral( "operatorPreflightSummary" ) ) );
  // selectStep site
  CHECK( blockContains( QStringLiteral( "void WorkflowSessionController::selectStep" ),
                        QStringLiteral( "operatorPreflightSummary" ) ) );
}
