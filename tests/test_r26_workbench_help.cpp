// test_r26_workbench_help.cpp — R26-02 workbench: help-system regression
// hardening for #1614's residual gap. The staged-degradation fix in
// HelpCompact::toJsonText (help_presenter.cpp) is in master; these tests pin
// the extreme budgets (below the {"id":""} shell size) that had no coverage,
// where the pre-fix code emitted sliced, unparseable JSON fragments.
#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QJsonObject>

#include "help/help_presenter.h"
#include "help/help_descriptor.h"

using namespace sicnu::help;

TEST_CASE( "HelpCompact::toJsonText stays parseable under extreme budgets (#1614 residual)",
           "[help][compact][r26-workbench]" )
{
  HelpDescriptor d;
  d.id = QStringLiteral( "operator.rs.r26_budget_probe" );
  d.kind = HelpKind::Operator;
  d.title = QStringLiteral( "预算边界探针" );
  d.summary = QStringLiteral( "summary text that must never leak as broken JSON" );
  AlgorithmPage page;
  page.keyParameters << QStringLiteral( "a" ) << QStringLiteral( "b" );
  d.algorithm = page;

  SECTION( "every tiny budget yields valid JSON" )
  {
    // Budgets from 2 up can honor the size cap; 0/1 cannot even fit the
    // two-character "{}" fallback, so for those the contract is only
    // "still parseable, never a sliced fragment".
    for ( int budget : { 2, 5, 8, 9 } )
    {
      const QString json = HelpCompact::toJsonText( d, budget );
      CAPTURE( budget, json );
      CHECK( json.size() <= budget );
      const QJsonDocument parsed = QJsonDocument::fromJson( json.toUtf8() );
      CHECK( parsed.isObject() );
    }
    for ( int budget : { 0, 1 } )
    {
      const QString json = HelpCompact::toJsonText( d, budget );
      CAPTURE( budget, json );
      CHECK( json == QStringLiteral( "{}" ) );
      const QJsonDocument parsed = QJsonDocument::fromJson( json.toUtf8() );
      CHECK( parsed.isObject() );
    }
  }

  SECTION( "a workable budget still carries the id" )
  {
    const QString json = HelpCompact::toJsonText( d, 60 );
    const QJsonDocument parsed = QJsonDocument::fromJson( json.toUtf8() );
    REQUIRE( parsed.isObject() );
    CHECK( parsed.object().contains( QStringLiteral( "id" ) ) );
  }
}
