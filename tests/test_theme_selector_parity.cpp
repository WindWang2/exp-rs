#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace {

QString readSource( const QString &relativePath )
{
  const QStringList candidates = {
    QStringLiteral( "%1/%2" ).arg( QStringLiteral( CMAKE_SOURCE_DIR ), relativePath ),
    QStringLiteral( "../%1" ).arg( relativePath ),
    QStringLiteral( "../../%1" ).arg( relativePath ),
    relativePath,
  };
  for ( const QString &path : candidates )
  {
    QFile f( path );
    if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
      return QString::fromUtf8( f.readAll() );
  }
  return {};
}

QSet<QString> extractSelectors( const QString &qssText )
{
  // Strip CSS comments /* ... */
  static const QRegularExpression commentRe( QStringLiteral( "/\\*.*?\\*/" ), QRegularExpression::DotMatchesEverythingOption );
  QString clean = qssText;
  clean.remove( commentRe );

  QSet<QString> selectors;
  static const QRegularExpression blockRe( QStringLiteral( "([^{}]+)\\{" ) );
  auto it = blockRe.globalMatch( clean );
  while ( it.hasNext() )
  {
    const auto m = it.next();
    const QString raw = m.captured( 1 ).trimmed();
    const QStringList parts = raw.split( ',' );
    for ( const QString &p : parts )
    {
      const QString normalized = p.simplified();
      if ( !normalized.isEmpty() )
        selectors.insert( normalized );
    }
  }
  return selectors;
}

} // namespace

TEST_CASE( "Theme: Light and Dark stylesheets exist and have selector parity", "[theme][parity]" )
{
  const QString lightQss = readSource( QStringLiteral( "resources/styles.qss" ) );
  const QString darkQss = readSource( QStringLiteral( "resources/styles-dark.qss" ) );

  REQUIRE_FALSE( lightQss.isEmpty() );
  REQUIRE_FALSE( darkQss.isEmpty() );

  const QSet<QString> lightSelectors = extractSelectors( lightQss );
  const QSet<QString> darkSelectors = extractSelectors( darkQss );

  REQUIRE( lightSelectors.size() > 50 );
  REQUIRE( darkSelectors.size() > 50 );

  // 1. Selector Parity Check
  QSet<QString> missingInDark = lightSelectors;
  missingInDark.subtract( darkSelectors );

  QSet<QString> missingInLight = darkSelectors;
  missingInLight.subtract( lightSelectors );

  INFO( "Missing in Dark: " + QStringList( missingInDark.values() ).join( ", " ).toStdString() );
  REQUIRE( missingInDark.isEmpty() );

  INFO( "Missing in Light: " + QStringList( missingInLight.values() ).join( ", " ).toStdString() );
  REQUIRE( missingInLight.isEmpty() );

  // 2. Syntax validation
  REQUIRE_FALSE( lightQss.contains( QStringLiteral( "border-bottom-color: none" ), Qt::CaseInsensitive ) );
  REQUIRE_FALSE( darkQss.contains( QStringLiteral( "border-bottom-color: none" ), Qt::CaseInsensitive ) );
  REQUIRE_FALSE( lightQss.contains( QRegularExpression( QStringLiteral( "\\bopacity\\s*:" ) ) ) );
  REQUIRE_FALSE( darkQss.contains( QRegularExpression( QStringLiteral( "\\bopacity\\s*:" ) ) ) );

  // 3. Color leak check
  REQUIRE_FALSE( darkQss.contains( QStringLiteral( "#f6f8fa" ), Qt::CaseInsensitive ) );

  // 4. Core shell selectors check
  const QStringList requiredCoreSelectors = {
    QStringLiteral( "#rsRibbonBar" ),
    QStringLiteral( "#rsRibbonQat" ),
    QStringLiteral( "#rsRibbonQatBtn" ),
    QStringLiteral( "#rsRibbonTabRow" ),
    QStringLiteral( "QPushButton#rsRibbonTabButton" ),
    QStringLiteral( "#rsRibbonCollapseBtn" ),
    QStringLiteral( "#rsRibbonLargeBtn" ),
    QStringLiteral( "#rsRibbonGroup" ),
    QStringLiteral( "#rsRibbonGroupTitle" ),
    QStringLiteral( "#rsRibbonSlider" ),
    QStringLiteral( "QComboBox#rsRibbonCombo" ),
    QStringLiteral( "#rsJobPanelDock" ),
    QStringLiteral( "#rsJobPanelHint" ),
    QStringLiteral( "#rsJobDetailView" ),
    QStringLiteral( "#rsJobLogView" ),
    QStringLiteral( "QLabel#rsReadyLabel" ),
    QStringLiteral( "QLabel#rsReadyBusy" )
  };

  for ( const QString &sel : requiredCoreSelectors )
  {
    INFO( "Checking core selector: " + sel.toStdString() );
    REQUIRE( lightSelectors.contains( sel ) );
    REQUIRE( darkSelectors.contains( sel ) );
  }
}

// ---------------------------------------------------------------------------
// UX 4.0 Milestone F: the C++ token layer (src/app/design_tokens.h) must stay in
// sync with the QSS design-token headers. The QSS header comments are the
// human-facing contract; the C++ constants are what runtime code reads.
// Changing one without the other silently splits the design system in two.
// ---------------------------------------------------------------------------

namespace {

bool qssHeaderContainsToken( const QString &qss, const QString &tokenHex )
{
    // The token header comment sits at the top of each QSS file.
    const QString head = qss.left( 1200 );
    return head.contains( tokenHex, Qt::CaseInsensitive );
}

} // namespace

TEST_CASE( "Theme: C++ design tokens mirror the QSS design tokens", "[theme][tokens][ux4]" )
{
  const QString lightQss = readSource( QStringLiteral( "resources/styles.qss" ) );
  const QString darkQss = readSource( QStringLiteral( "resources/styles-dark.qss" ) );
  const QString tokens = readSource( QStringLiteral( "src/app/design_tokens.h" ) );

  REQUIRE_FALSE( lightQss.isEmpty() );
  REQUIRE_FALSE( darkQss.isEmpty() );
  REQUIRE_FALSE( tokens.isEmpty() );

  // QSS header tokens ↔ C++ light-theme constants.
  const QStringList lightTokens = {
    QStringLiteral( "#F4F6F8" ), QStringLiteral( "#FFFFFF" ),
    QStringLiteral( "#1C2430" ), QStringLiteral( "#5A6573" ),
    QStringLiteral( "#0B6E4F" ), QStringLiteral( "#1A7F37" ),
    QStringLiteral( "#B58100" ), QStringLiteral( "#C9372C" ),
    QStringLiteral( "#6E56CF" ),
  };
  for ( const QString &hex : lightTokens )
  {
    INFO( hex.toStdString() );
    REQUIRE( qssHeaderContainsToken( lightQss, hex ) );
    REQUIRE( tokens.contains( hex, Qt::CaseInsensitive ) );
  }

  // QSS dark tokens ↔ C++ dark-theme constants.
  const QStringList darkTokens = {
    QStringLiteral( "#1A1D23" ), QStringLiteral( "#1E2229" ),
    QStringLiteral( "#E8ECF1" ), QStringLiteral( "#A8B0BC" ),
    QStringLiteral( "#2BB673" ), QStringLiteral( "#3DCF6A" ),
    QStringLiteral( "#F07167" ), QStringLiteral( "#4DA3E0" ),
  };
  for ( const QString &hex : darkTokens )
  {
    INFO( hex.toStdString() );
    REQUIRE( tokens.contains( hex, Qt::CaseInsensitive ) );
  }

  // The pipeline editor's canvas badge palette is the single documented
  // exception: a deliberately dark slate graphics scene in both themes
  // (docs/ui-architecture.md#exceptions). Its colors must not leak into
  // panel/table code — a broader statusColor re-definition there would
  // reintroduce the pre-4.0 divergence.
  const QString jobPanel = readSource( QStringLiteral( "src/app/shell/rs_job_panel.cpp" ) );
  REQUIRE( jobPanel.contains( QStringLiteral( "SicnuUi::Tokens::statusOk" ) ) );
  REQUIRE( jobPanel.contains( QStringLiteral( "SicnuUi::Tokens::themeIsDark" ) ) );
  const QString georefList =
    readSource( QStringLiteral( "src/app/georeferencer/rs_georef_task_list.cpp" ) );
  REQUIRE( georefList.contains( QStringLiteral( "SicnuUi::Tokens::statusOk" ) ) );
}
