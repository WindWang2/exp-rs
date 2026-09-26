/***************************************************************************
 * test_i18n_migrated_translations.cpp — Track 17 R4 (WP-F guard)
 *
 * Guards the zh_CN backfill entries created by the WP-B migrations.
 *
 * WHY THIS EXISTS: the current lupdate (6.11) attributes tr() calls in
 * namespace-qualified classes (e.g. sicnu::app::CartographyDock) to the
 * fully-qualified context name, while the Qt runtime looks translations up
 * under the moc short class name (CartographyDock) — which is also the
 * form the pre-existing tracked .ts uses. Running `sicnu_i18n_update`
 * (-no-obsolete) with today's lupdate would therefore drop the short-name
 * entries this track added. Nothing else in the suite would notice (the
 * .ts completeness gate only counts unfinished entries), and the failure
 * mode is silent: Chinese-locale users would see English source text.
 *
 * This gate pins a representative sample of migrated (context, source,
 * translation) triples so the regression is loud.
 ***************************************************************************/
#include <catch2/catch.hpp>

#include <QFile>
#include <QString>

#include <cstring>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace {

QString loadTs()
{
  QFile f( QStringLiteral( CMAKE_SOURCE_DIR "/resources/translations/sicnu_zh_CN.ts" ) );
  REQUIRE( f.open( QIODevice::ReadOnly ) );
  return QString::fromUtf8( f.readAll() );
}

/// True when <context><name>ctx</name> ... <source>src</source> carries a
/// non-empty <translation>. Text-level matching (same technique as
/// test_i18n's NOOP gates); sources here contain no XML-special chars
/// except '<'/'&' which are escaped exactly as lupdate stores them.
bool hasTranslation( const QString &ts, const QString &ctx, const QString &src )
{
  const QString ctxBlock = QStringLiteral(
      "<context>\n    <name>%1</name>" ).arg( ctx );
  const int ctxStart = ts.indexOf( ctxBlock );
  if ( ctxStart < 0 )
    return false;
  const int ctxEnd = ts.indexOf( QStringLiteral( "</context>" ), ctxStart );
  if ( ctxEnd < 0 )
    return false;
  const QString body = ts.mid( ctxStart, ctxEnd - ctxStart );
  const QString needle = QStringLiteral( "<source>%1</source>" ).arg( src );
  const int srcPos = body.indexOf( needle );
  if ( srcPos < 0 )
    return false;
  const int trPos = body.indexOf( QStringLiteral( "<translation>" ), srcPos );
  const int msgEnd = body.indexOf( QStringLiteral( "</message>" ), srcPos );
  if ( trPos < 0 || msgEnd < 0 || trPos > msgEnd )
    return false;
  const int trStop = body.indexOf( QStringLiteral( "</translation>" ), trPos );
  return body.mid( trPos + strlen( "<translation>" ),
                   trStop - trPos - strlen( "<translation>" ) )
      .trimmed()
      .isEmpty() == false;
}

} // namespace

TEST_CASE( "WP-B migrated strings keep their zh_CN translations under the",
           "moc short-name contexts" )
{
  const QString ts = loadTs();

  struct Triple
  {
    const char *ctx;
    const char *src;
  };
  // One representative per migrated context (contexts: moc short names).
  const std::vector<Triple> triples = {
    { "CartographyDock", "Cartography Workbench" },
    { "AgentCopilotDockWidget", "AI Copilot Assistant" },
    { "LabCockpitDock", "Capsule exported: %1 (%2 bytes)" },
    { "VaWorkbenchPanel", "Per-band means (sampled estimate %1×%2)" },
    { "VaCursorProbe", "Cursor coordinates could not be transformed to the layer CRS." },
    { "LlmSettingsDialog", "Connection failed: %1" },
    { "GuidedWorkflowWidget", "Run this step" },
    { "StepExplanationPanel", "Cannot explain this step" },
    { "RsOperatorCatalogPanel", "Remote Sensing Operator Catalog" },
    { "QgisDesktopWindow", "Undergraduate Lab Teaching Workbench" },
    { "QObject", "Undergraduate Lab Teaching Workbench" },
  };

  QStringList missing;
  for ( const Triple &t : triples )
  {
    if ( !hasTranslation( ts, QString::fromLatin1( t.ctx ), QString::fromLatin1( t.src ) ) )
      missing << QStringLiteral( "%1 / %2" ).arg( t.ctx, t.src );
  }
  for ( const QString &m : missing )
    UNSCOPED_INFO( "missing or empty zh_CN translation: " << m.toStdString() );
  CHECK( missing.isEmpty() );
}
