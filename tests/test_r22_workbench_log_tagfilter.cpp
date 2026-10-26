// test_r22_workbench_log_tagfilter.cpp — R22-05 W5
//
// The log panel's Tags combo is editable: a free-typed tag (or partial tag)
// must filter the retained messages. The old condition keyed off
// currentData(), which is empty while the "All" item is current and the user
// is typing — so the typed filter was silently ignored and the visible count
// never changed (plus the condition carried a copy-pasted duplicate clause).
#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QLabel>

#include "app/log_panel.h"
#include <qgsmessagelog.h>

namespace
{

QLabel *countLabel( LogPanel &panel )
{
  const auto labels = panel.findChildren<QLabel *>();
  for ( QLabel *label : labels )
  {
    if ( label->text().contains( QStringLiteral( "messages" ) ) )
      return label;
  }
  return nullptr;
}

int visibleCount( LogPanel &panel )
{
  QLabel *label = countLabel( panel );
  REQUIRE( label != nullptr );
  // Label text is tr("%1 messages").
  return label->text().section( QLatin1Char( ' ' ), 0, 0 ).toInt();
}

} // namespace

TEST_CASE( "r22 W5: typed tag filter narrows the retained messages",
           "[r22][workbench][log_panel]" )
{
  LogPanel panel;
  panel.clearMessages();
  panel.logMessage( QStringLiteral( "alpha spectrum" ), QStringLiteral( "ndvi" ), Qgis::MessageLevel::Info );
  panel.logMessage( QStringLiteral( "beta granule" ), QStringLiteral( "s2" ), Qgis::MessageLevel::Info );
  REQUIRE( visibleCount( panel ) == 2 );

  // The tag combo is the only editable one (the level combo next to it is not).
  QComboBox *tagFilter = nullptr;
  for ( QComboBox *combo : panel.findChildren<QComboBox *>() )
  {
    if ( combo->isEditable() )
      tagFilter = combo;
  }
  REQUIRE( tagFilter != nullptr );
  REQUIRE( tagFilter->isEditable() );
  REQUIRE( tagFilter->count() >= 3 ); // All + ndvi + s2 (auto-added items)

  SECTION( "free-typed partial tag filters (the typed-filter bug)" )
  {
    // "ndv" matches no combo item, so the current item stays "All" — the
    // filter must still take the typed text into account.
    tagFilter->setCurrentText( QStringLiteral( "ndv" ) );
    CHECK( visibleCount( panel ) == 1 );
  }

  SECTION( "selecting an existing tag item filters" )
  {
    const int index = tagFilter->findText( QStringLiteral( "s2" ) );
    REQUIRE( index > 0 );
    tagFilter->setCurrentIndex( index );
    CHECK( visibleCount( panel ) == 1 );
  }

  SECTION( "back on All everything is visible again" )
  {
    tagFilter->setCurrentIndex( tagFilter->findText( QStringLiteral( "s2" ) ) );
    REQUIRE( visibleCount( panel ) == 1 );
    tagFilter->setCurrentIndex( 0 );
    CHECK( visibleCount( panel ) == 2 );
  }
}
