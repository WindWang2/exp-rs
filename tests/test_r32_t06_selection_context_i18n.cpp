// tests/test_r32_t06_selection_context_i18n.cpp
// R32-T06 WP03 (#2083) — the disabled-command reason must actually reach the
// zh_CN catalog entry that lupdate produced.
//
// `requirementFacts()` declares its 16 labels with QT_TR_NOOP inside
// `namespace ContextRules`, so lupdate keys them under the fully-qualified
// namespace context `sicnu::app::ContextRules`. The runtime lookup used
// `QObject::tr()`, which asks the translator for context "QObject" — a context
// that holds the palette's next-action strings but none of the requirement
// labels. The result was an English "A vector layer must be selected" for a
// Chinese user on every disabled command, while the .ts file already carried
// the translations.
//
// This test binds the real .qm (built by the sicnu_i18n target from the
// tracked .ts) to the real derivation, so it fails if either side drifts:
// wrong context, missing catalog, or an untranslated label.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/selection_context.h"

#include <qgsapplication.h>
#include <qgsproject.h>

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTranslator>

#include <cstdio>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using namespace sicnu::app;

namespace
{

/// RAII so a failing assertion can never leave zh_CN installed for the rest of
/// the binary.
struct InstalledTranslator
{
  QTranslator translator;
  bool installed = false;
  ~InstalledTranslator()
  {
    if ( installed )
      QCoreApplication::removeTranslator( &translator );
  }
};

} // namespace

TEST_CASE( "disabled-command reasons resolve in the zh_CN catalog", "[r32t06][wp03][i18n]" )
{
  const SelectionContextSnapshot nothingSelected; // default = empty workspace

  // --- English baseline: no translator installed -------------------------
  // The machine code must be identical in every language; only the label is
  // user text. This is what keeps command dispatch and help drift checks
  // working across locales.
  {
    const auto facts = ContextRules::requirementFacts( nothingSelected, "layer.toggleEditing" );
    REQUIRE_FALSE( facts.isEmpty() );
    CHECK( facts.front().code == QStringLiteral( "vector.selected" ) );
    CHECK( facts.front().label == QStringLiteral( "A vector layer must be selected" ) );
    CHECK_FALSE( facts.front().satisfied );
  }

  // --- zh_CN: bind the catalog the product actually ships ----------------
  InstalledTranslator zh;
  const QString qmPath = QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR "/sicnu_zh_CN.qm" );
  INFO( "catalog: " << qmPath.toStdString() );
  REQUIRE( QFile::exists( qmPath ) );
  REQUIRE( zh.translator.load( qmPath ) );
  REQUIRE( QCoreApplication::installTranslator( &zh.translator ) );
  zh.installed = true;

  SECTION( "the first unsatisfied requirement is Chinese, the code is not" )
  {
    const auto facts = ContextRules::requirementFacts( nothingSelected, "layer.toggleEditing" );
    REQUIRE_FALSE( facts.isEmpty() );
    CHECK( facts.front().code == QStringLiteral( "vector.selected" ) );
    CHECK( facts.front().label == QString::fromUtf8( "必须选择一个矢量图层" ) );

    // The text projection and the structured facts channel share one
    // derivation, so the reason the palette/tooltip shows is that same label.
    CHECK( ContextRules::unavailabilityReason( nothingSelected, "layer.toggleEditing" ) ==
           QString::fromUtf8( "必须选择一个矢量图层" ) );
  }

  SECTION( "a different command family is translated too (not one lucky string)" )
  {
    const auto facts = ContextRules::requirementFacts( nothingSelected, "cartography.compose" );
    REQUIRE_FALSE( facts.isEmpty() );
    CHECK( facts.front().code == QStringLiteral( "map.layers_present" ) );
    CHECK( facts.front().label == QString::fromUtf8( "排版前请先向画布添加地图图层" ) );
    CHECK( ContextRules::unavailabilityReason( nothingSelected, "cartography.compose" ) ==
           QString::fromUtf8( "排版前请先向画布添加地图图层" ) );
  }

  SECTION( "every requirement label of a multi-row command is translated" )
  {
    // `rs.speckle` spells out both rows (raster AND sar) so the disabled state
    // always names the missing condition — both rows must survive the switch.
    const auto facts = ContextRules::requirementFacts( nothingSelected, "rs.speckle" );
    REQUIRE( facts.size() >= 2 );
    CHECK( facts.at( 0 ).code == QStringLiteral( "raster.selected" ) );
    CHECK( facts.at( 0 ).label == QString::fromUtf8( "必须选择一个栅格图层" ) );
    CHECK( facts.at( 1 ).code == QStringLiteral( "sar.selected" ) );
    CHECK( facts.at( 1 ).label == QString::fromUtf8( "必须选择 SAR 数据" ) );
  }

  SECTION( "the old QObject context has no entry for these labels" )
  {
    // This is the defect itself, kept as a permanent differential control: the
    // pre-fix lookup asked for context "QObject", which carries the palette's
    // next-action strings but none of the 16 requirement labels, so Qt fell
    // back to the source text. If someone "fixes" the catalog by duplicating
    // the entries into QObject instead, this check turns red and points at the
    // real problem.
    CHECK( QCoreApplication::translate( "QObject", "A vector layer must be selected" ) ==
           QStringLiteral( "A vector layer must be selected" ) );
    // ...while the context lupdate actually emitted is present.
    CHECK( QCoreApplication::translate( "sicnu::app::ContextRules", "A vector layer must be selected" ) ==
           QString::fromUtf8( "必须选择一个矢量图层" ) );
  }

  SECTION( "removing the catalog returns English, proving the catalog is the source" )
  {
    QCoreApplication::removeTranslator( &zh.translator );
    zh.installed = false;
    const auto facts = ContextRules::requirementFacts( nothingSelected, "layer.toggleEditing" );
    REQUIRE_FALSE( facts.isEmpty() );
    CHECK( facts.front().label == QStringLiteral( "A vector layer must be selected" ) );
  }
}
