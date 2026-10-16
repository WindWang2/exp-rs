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

  // 3. Color leak check — signature literals of each theme (canvas/accent/ink).
  // The old probe used "#f6f8fa", a stale literal absent from both stylesheets,
  // so it guarded nothing. Each literal below is verified to appear in its own
  // stylesheet's body (positive control) and must never appear in the other
  // theme's file, so a copy-paste or wrong-theme drift between the two files
  // fails here. The full disjointness of the two palettes is enforced by the
  // body-drift gate in the token test case below.
  const QStringList lightSignatureHexes = {
    QStringLiteral( "#F4F6F8" ),  // surface.canvas
    QStringLiteral( "#0B6E4F" ),  // accent
    QStringLiteral( "#1C2430" ),  // ink.primary
  };
  for ( const QString &hex : lightSignatureHexes )
  {
    INFO( "Light signature literal missing from light QSS: " + hex.toStdString() );
    REQUIRE( lightQss.contains( hex, Qt::CaseInsensitive ) );
    INFO( "Light literal leaked into dark QSS: " + hex.toStdString() );
    REQUIRE_FALSE( darkQss.contains( hex, Qt::CaseInsensitive ) );
  }
  const QStringList darkSignatureHexes = {
    QStringLiteral( "#1A1D23" ),  // surface.canvas
    QStringLiteral( "#2BB673" ),  // accent
    QStringLiteral( "#E8ECF1" ),  // ink.primary
  };
  for ( const QString &hex : darkSignatureHexes )
  {
    INFO( "Dark signature literal missing from dark QSS: " + hex.toStdString() );
    REQUIRE( darkQss.contains( hex, Qt::CaseInsensitive ) );
    INFO( "Dark literal leaked into light QSS: " + hex.toStdString() );
    REQUIRE_FALSE( lightQss.contains( hex, Qt::CaseInsensitive ) );
  }

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

// The design-token header is the first /* ... */ comment block of each QSS
// file. Light lists one role per line ("* accent  #0B6E4F  hover #095C42",
// "* state.ok/warn/err  #1A7F37 / #B58100 / #C9372C"); dark compresses roles
// onto middle-dot-separated lines ("* accent #2BB673 · mapSelect #4DA3E0").
// In both layouts the token values are plain #RRGGBB literals and nothing
// else in that block is hex, so extracting every hex literal from the block
// yields exactly the token inventory — robust to either formatting style.
QString qssTokenHeader( const QString &qssText )
{
    const int start = qssText.indexOf( QStringLiteral( "/*" ) );
    if ( start < 0 )
        return {};
    const int end = qssText.indexOf( QStringLiteral( "*/" ), start );
    if ( end < 0 )
        return qssText.mid( start );
    return qssText.mid( start, end - start + 2 );
}

QSet<QString> extractHexLiterals( const QString &text )
{
    static const QRegularExpression hexRe( QStringLiteral( "#[0-9A-Fa-f]{6}\\b" ) );
    QSet<QString> hexes;
    auto it = hexRe.globalMatch( text );
    while ( it.hasNext() )
        hexes.insert( it.next().captured( 0 ).toUpper() );
    return hexes;
}

// Hex literals of the stylesheet body: everything after the token header.
QSet<QString> extractBodyHexLiterals( const QString &qssText )
{
    const int end = qssText.indexOf( QStringLiteral( "*/" ) );
    return extractHexLiterals( end < 0 ? qssText : qssText.mid( end + 2 ) );
}

// Parses the QColor( 0xRR, 0xGG, 0xBB ) constants of one palette namespace
// out of design_tokens.h and returns them as #RRGGBB strings. Matching on
// the parsed values (instead of maintaining a header-name -> constant-name
// mapping table) keeps the check correct for any constant added later; the
// only static data is the dark-header residual list in the test below.
QSet<QString> parsePaletteConstants( const QString &tokensText, const QString &ns )
{
    const QString opener = QStringLiteral( "namespace %1 {" ).arg( ns );
    const QString closer = QStringLiteral( "} // namespace %1" ).arg( ns );
    const int start = tokensText.indexOf( opener );
    const int end = tokensText.indexOf( closer );
    if ( start < 0 || end < 0 || end <= start )
        return {};
    const QString block = tokensText.mid( start, end - start );
    static const QRegularExpression qcolorRe( QStringLiteral(
        "QColor\\(\\s*0x([0-9A-Fa-f]{2}),\\s*0x([0-9A-Fa-f]{2}),\\s*0x([0-9A-Fa-f]{2})\\s*\\)" ) );
    QSet<QString> hexes;
    auto it = qcolorRe.globalMatch( block );
    while ( it.hasNext() )
    {
        const auto m = it.next();
        hexes.insert( QStringLiteral( "#%1%2%3" )
                          .arg( m.captured( 1 ), m.captured( 2 ), m.captured( 3 ) )
                          .toUpper() );
    }
    return hexes;
}

QStringList sortedHexList( const QSet<QString> &hexes )
{
    QStringList list = hexes.values();
    list.sort();
    return list;
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

    // Parse the token inventories instead of hardcoding them. The previous
    // version pinned 9 light + 8 dark hexes; the unlisted ones could drift
    // between QSS and design_tokens.h without any test noticing (CARTO-13).
    const QSet<QString> lightHeader = extractHexLiterals( qssTokenHeader( lightQss ) );
    const QSet<QString> darkHeader = extractHexLiterals( qssTokenHeader( darkQss ) );
    const QSet<QString> lightConstants =
        parsePaletteConstants( tokens, QStringLiteral( "Light" ) );
    const QSet<QString> darkConstants =
        parsePaletteConstants( tokens, QStringLiteral( "Dark" ) );

    REQUIRE_FALSE( lightHeader.isEmpty() );
    REQUIRE_FALSE( darkHeader.isEmpty() );
    REQUIRE_FALSE( lightConstants.isEmpty() );
    REQUIRE_FALSE( darkConstants.isEmpty() );

    // Wholesale deletion of a token header (or gutting of a palette) must
    // fail. Full inventories today: 17 light header tokens mirrored by 17
    // Light constants; 10 dark header tokens under 13 unique Dark constants
    // (14 constants — #3DCF6A serves both accentHover and ok). The floors
    // are pinned at the real counts; growth is welcome, shrinkage is drift.
    REQUIRE( lightHeader.size() >= 17 );
    REQUIRE( darkHeader.size() >= 10 );
    REQUIRE( lightConstants.size() >= 17 );
    REQUIRE( darkConstants.size() >= 13 );

    // Forward direction: every QSS header token exists as a C++ palette
    // constant of the same theme (previously only 9 light / 8 dark were
    // checked; now all 17 light and 10 dark header tokens are covered).
    for ( const QString &hex : lightHeader )
    {
        INFO( "Light QSS header token without design_tokens.h Light constant: "
              + hex.toStdString() );
        REQUIRE( lightConstants.contains( hex ) );
    }
    for ( const QString &hex : darkHeader )
    {
        INFO( "Dark QSS header token without design_tokens.h Dark constant: "
              + hex.toStdString() );
        REQUIRE( darkConstants.contains( hex ) );
    }

    // Reverse direction, light: the Light palette mirrors the light header
    // one-to-one today, so a Light constant without a header entry means the
    // human-facing contract was not updated.
    {
        QSet<QString> unlisted = lightConstants;
        unlisted.subtract( lightHeader );
        INFO( "Light constants missing from styles.qss header: "
              + sortedHexList( unlisted ).join( ", " ).toStdString() );
        REQUIRE( unlisted.isEmpty() );
    }

    // Reverse direction, dark: the dark header is a condensed summary that
    // omits the line/ai roles, so exactly these constants may stay unlisted.
    // Static data that must be maintained: add a dark header token or a Dark
    // constant -> update this residual list (either direction fails below).
    const QStringList darkHeaderResidualList = {
        QStringLiteral( "#343B46" ),  // Dark::lineSubtle
        QStringLiteral( "#4A5260" ),  // Dark::lineStrong
        QStringLiteral( "#9D8CFF" ),  // Dark::ai
    };
    const QSet<QString> darkHeaderResidual(
        darkHeaderResidualList.cbegin(), darkHeaderResidualList.cend() );
    {
        QSet<QString> unlisted = darkConstants;
        unlisted.subtract( darkHeader );
        QSet<QString> unexpected = unlisted;
        unexpected.subtract( darkHeaderResidual );
        QSet<QString> documentedButGone = darkHeaderResidual;
        documentedButGone.subtract( unlisted );
        INFO( "Dark constants beyond the documented residual (tokenize or extend the residual): "
              + sortedHexList( unexpected ).join( ", " ).toStdString() );
        REQUIRE( unexpected.isEmpty() );
        INFO( "Residual hexes now listed in the dark header (prune darkHeaderResidual): "
              + sortedHexList( documentedButGone ).join( ", " ).toStdString() );
        REQUIRE( documentedButGone.isEmpty() );
    }

    // Body drift gate: every hex literal used in a stylesheet body must be a
    // design token of that theme (header or C++ constant) or a member of the
    // explicit allowlist below. A newly invented or pasted-in raw hex fails
    // until it is promoted to a token or added here with a justification.
    // Cross-theme leaks are caught for free: a light token hex pasted into
    // the dark body is neither a dark token nor allowlisted there (and vice
    // versa) — the two palettes are fully disjoint today.
    //
    // Current residual, 26 hexes in total (14 light + 12 dark), enumerated in
    // full because the set is small: derived shades, tints, gradient stops
    // and one-off instrument colors with no token counterpart yet. Notable
    // near-token drifts pinned here on purpose: #239A61 is the dark QSS
    // "ac-2" while Dark::accentHover is #3DCF6A; #A48AFB is the dark QSS
    // "ai" while Dark::ai is #9D8CFF; #E0A82E is the dark busy-indicator
    // text while Dark::warn is #E0A458; #8A93A0/#8B93A0 are the disabled
    // text colors with no token at all.
    const QStringList lightBodyAllowlistList = {
        QStringLiteral( "#063C2B" ),  // primary button focus border (accent shade)
        QStringLiteral( "#1C1F24" ),  // histogram chart well (dark instrument bg)
        QStringLiteral( "#8A93A0" ),  // disabled text
        QStringLiteral( "#8C5B00" ),  // busy-indicator text (warn shade)
        QStringLiteral( "#A8B1BC" ),  // pressed button bg / borders (bg-6)
        QStringLiteral( "#B8E2D1" ),  // ok-tint border (secondary map header active)
        QStringLiteral( "#E6F4EE" ),  // ok-tint background
        QStringLiteral( "#E8ECF0" ),  // map canvas background
        QStringLiteral( "#F0F7F4" ),  // progress bar gradient stop (ok tint)
        QStringLiteral( "#F4F5F7" ),  // secondary map view header bg
        QStringLiteral( "#F4F6F9" ),  // menu bar gradient stop
        QStringLiteral( "#F6F8FA" ),  // progress bar gradient stop (paper tint)
        QStringLiteral( "#F8FAFB" ),  // scrollbar / gradient stop
        QStringLiteral( "#FAFBFC" ),  // ribbon page background
    };
    const QStringList darkBodyAllowlistList = {
        QStringLiteral( "#10151B" ),  // text on accent chips (flow numbers)
        QStringLiteral( "#12141A" ),  // histogram chart well
        QStringLiteral( "#14171C" ),  // preset description bg
        QStringLiteral( "#1A2A24" ),  // progress bar gradient stop (ok tint)
        QStringLiteral( "#1B382B" ),  // ok-tint background
        QStringLiteral( "#22262E" ),  // scrollbar / gradient stop
        QStringLiteral( "#239A61" ),  // accent hover "ac-2" (drift: Dark::accentHover is #3DCF6A)
        QStringLiteral( "#2A303A" ),  // map canvas background
        QStringLiteral( "#6B7380" ),  // pressed button bg / borders (bg-6)
        QStringLiteral( "#8B93A0" ),  // disabled text
        QStringLiteral( "#A48AFB" ),  // ai accent (drift: Dark::ai is #9D8CFF)
        QStringLiteral( "#E0A82E" ),  // busy-indicator text (drift: Dark::warn is #E0A458)
    };
    const QSet<QString> lightBodyAllowlist(
        lightBodyAllowlistList.cbegin(), lightBodyAllowlistList.cend() );
    const QSet<QString> darkBodyAllowlist(
        darkBodyAllowlistList.cbegin(), darkBodyAllowlistList.cend() );

    QSet<QString> lightTokenHexes = lightHeader;
    lightTokenHexes.unite( lightConstants );
    {
        QSet<QString> unexplained = extractBodyHexLiterals( lightQss );
        unexplained.subtract( lightTokenHexes );
        unexplained.subtract( lightBodyAllowlist );
        INFO( "New raw hex literals in styles.qss body (tokenize or allowlist): "
              + sortedHexList( unexplained ).join( ", " ).toStdString() );
        REQUIRE( unexplained.isEmpty() );
    }

    QSet<QString> darkTokenHexes = darkHeader;
    darkTokenHexes.unite( darkConstants );
    {
        QSet<QString> unexplained = extractBodyHexLiterals( darkQss );
        unexplained.subtract( darkTokenHexes );
        unexplained.subtract( darkBodyAllowlist );
        INFO( "New raw hex literals in styles-dark.qss body (tokenize or allowlist): "
              + sortedHexList( unexplained ).join( ", " ).toStdString() );
        REQUIRE( unexplained.isEmpty() );
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
