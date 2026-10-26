// test_r22_workbench_temporal_pages.cpp — R22-05 W2
//
// Every algorithm entry in the Temporal Analysis dialog must switch
// m_paramStack to the page holding that algorithm's parameters. Three
// entries added with Temporal Intelligence 11.0 (seasonal breaks, model
// selection, phenology multi) were never wired into algorithmChanged();
// selecting them left the summary page visible, so the study silently ran
// with hard-coded defaults the user could not see or change.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QFrame>
#include <QStackedWidget>

#include <qgsapplication.h>
#include <qgsproject.h>

#include "app/dialogs/temporal_analysis_dialog.h"

namespace
{

struct PageCase
{
  const char *algorithmId;
  int expectedPage;
  const char *marker; // objectName of a widget that must live on that page
};

const PageCase kPages[] = {
  { "rs:temporal_summary", 0, "temporalMedianCheck" },
  { "rs:temporal_composite", 1, "temporalCompositeMethod" },
  { "rs:temporal_index_series", 2, "temporalIndexCombo" },
  { "rs:temporal_trend", 3, nullptr }, // hint-label-only page
  { "rs:temporal_anomaly", 4, "temporalAnomalyMethod" },
  { "rs:temporal_extract_series", 5, "temporalPointEdit" },
  { "rs:temporal_seasonal_breaks", 6, "temporalSeasonalHarmonics" },
  { "rs:temporal_model_select", 7, "temporalSelectPenalty" },
  { "rs:temporal_phenology_multi", 8, "temporalPhenoCycles" },
};

} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "r22 W2: each temporal algorithm selects its own parameter page",
           "[r22][workbench][temporal]" )
{
  TemporalAnalysisDialog dialog;

  auto *combo = dialog.findChild<QComboBox *>( QStringLiteral( "temporalAlgorithmCombo" ) );
  auto *stack = dialog.findChild<QStackedWidget *>( QStringLiteral( "temporalParamStack" ) );
  REQUIRE( combo != nullptr );
  REQUIRE( stack != nullptr );
  REQUIRE( combo->count() == 9 );
  REQUIRE( stack->count() == 9 );

  for ( const PageCase &c : kPages )
  {
    CAPTURE( c.algorithmId );
    const int index = combo->findData( QString::fromLatin1( c.algorithmId ) );
    REQUIRE( index >= 0 );
    combo->setCurrentIndex( index );

    CHECK( stack->currentIndex() == c.expectedPage );
    if ( c.marker )
    {
      // The marker widget must be on the visible page — not merely exist
      // somewhere in the dialog.
      CHECK( stack->currentWidget()->findChild<QWidget *>( QLatin1String( c.marker ) ) != nullptr );
    }
  }
}
