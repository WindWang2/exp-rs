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

  // 3. Color leak check
  REQUIRE_FALSE( darkQss.contains( QStringLiteral( "#f6f8fa" ), Qt::CaseInsensitive ) );

  // 4. Core shell selectors check
  const QStringList requiredCoreSelectors = {
    QStringLiteral( "#rsRibbonBar" ),
    QStringLiteral( "#rsRibbonQat" ),
    QStringLiteral( "#rsRibbonQatBtn" ),
    QStringLiteral( "#rsRibbonTabRow" ),
    QStringLiteral( "QPushButton#rsRibbonTabButton" ),
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
