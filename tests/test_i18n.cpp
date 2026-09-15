// tests/test_i18n.cpp — D6 i18n guards
//
// (a) resources/translations/sicnu_zh_CN.ts: zero unfinished entries, en source
//     language, zh_CN target;
// (b) data/terms/rs_glossary.json: ≥300 terms, schema-clean, resolves through
//     the terminology provider, searchable via the single help index;
// (c) audited modules: no user-visible string literal passed to a widget API
//     without tr().
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QString>
#include <QXmlStreamReader>

#include <QRegularExpression>

#include "help/help_registry.h"
#include "help/help_search_index.h"
#include "help/terminology_provider.h"

using namespace sicnu::help;

namespace
{

QString readSourceTreeFile( const QString &relativePath )
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

TerminologyProvider::LoadResult loadGlossary()
{
  const QString text = readSourceTreeFile( QStringLiteral( "data/terms/rs_glossary.json" ) );
  return TerminologyProvider::loadFromJson( text.toUtf8(), QStringLiteral( "rs_glossary.json" ) );
}

// Modules named by the D6 goal as audited for unwrapped user-visible strings,
// plus the panels/datasets surfaces the guided workflows rely on.
QStringList auditedModules()
{
  return {
    QStringLiteral( "src/app/classification/rs_classify_flowchart_widget.cpp" ),
    QStringLiteral( "src/app/dialogs/batch_processing_dialog.cpp" ),
    QStringLiteral( "src/app/dialogs/help_viewer_dialog.cpp" ),
    QStringLiteral( "src/app/dialogs/preferences_dialog.cpp" ),
    QStringLiteral( "src/app/dialogs/product_import_dialog.cpp" ),
    QStringLiteral( "src/app/dialogs/raster_processing_dialog_base.h" ),
    QStringLiteral( "src/app/main_window_menus.cpp" ),
    QStringLiteral( "src/app/widgets/guided_workflow_widget.cpp" ),
    QStringLiteral( "src/app/widgets/progress_dialog.cpp" ),
    QStringLiteral( "src/app/help/availability_facts_adapter.cpp" ),
  };
}

} // namespace

TEST_CASE( "zh_CN translation file is complete", "[i18n]" )
{
  const QString ts = readSourceTreeFile( QStringLiteral( "resources/translations/sicnu_zh_CN.ts" ) );
  REQUIRE( !ts.isEmpty() );

  QXmlStreamReader xml( ts );
  int messages = 0;
  int unfinished = 0;
  int empty = 0;
  QString language, sourceLanguage;
  while ( !xml.atEnd() )
  {
    xml.readNext();
    if ( xml.isStartElement() && xml.name() == QLatin1String( "TS" ) )
    {
      language = xml.attributes().value( QStringLiteral( "language" ) ).toString();
      sourceLanguage = xml.attributes().value( QStringLiteral( "sourcelanguage" ) ).toString();
    }
    if ( xml.isStartElement() && xml.name() == QLatin1String( "message" ) )
    {
      ++messages;
      while ( !( xml.isEndElement() && xml.name() == QLatin1String( "message" ) ) && !xml.atEnd() )
      {
        xml.readNext();
        if ( xml.isStartElement() && xml.name() == QLatin1String( "translation" ) )
        {
          if ( xml.attributes().hasAttribute( QStringLiteral( "type" ) ) )
            ++unfinished;
          // numerus translations contain <numerusform> children: collect text tokens
          QString text;
          int depth = 0;
          while ( !( xml.isEndElement() && xml.name() == QLatin1String( "translation" ) ) && !xml.atEnd() )
          {
            xml.readNext();
            if ( xml.isStartElement() && xml.name() == QLatin1String( "numerusform" ) )
              ++depth;
            if ( xml.isCharacters() )
              text += xml.text();
            if ( xml.isEndElement() && xml.name() == QLatin1String( "numerusform" ) && depth > 0 )
              --depth;
          }
          if ( text.trimmed().isEmpty() )
            ++empty;
        }
      }
    }
  }
  INFO( "xml error: " << xml.errorString().toStdString() << " @line " << xml.lineNumber() );
  REQUIRE_FALSE( xml.hasError() );
  INFO( "messages: " << messages );
  CHECK( sourceLanguage == QLatin1String( "en" ) );
  CHECK( language == QLatin1String( "zh_CN" ) );
  // The pipeline covers the swept src/app surfaces; guard against regressions
  // that would silently shrink the translation layer.
  CHECK( messages >= 3000 );
  CHECK( unfinished == 0 );
  CHECK( empty == 0 );
}

TEST_CASE( "glossary loads, validates and resolves", "[i18n][terms]" )
{
  TerminologyProvider::LoadResult result = loadGlossary();
  INFO( "errors: " << result.errors.join( QStringLiteral( "; " ) ).toStdString() );
  REQUIRE( result.errors.isEmpty() );
  REQUIRE( result.terms.size() >= 300 );

  QStringList categories;
  for ( const TermEntry &term : result.terms )
    if ( !categories.contains( term.category ) )
      categories << term.category;
  CHECK( categories.size() == 12 );

  TerminologyProvider provider( result.terms );
  REQUIRE( provider.lookup( QStringLiteral( "NDVI" ) ) != nullptr );                       // alias
  REQUIRE( provider.lookup( QStringLiteral( "辐射定标" ) ) != nullptr );                    // zh
  REQUIRE( provider.lookup( QStringLiteral( "radiometric calibration" ) ) != nullptr );    // en
  CHECK( provider.lookup( QStringLiteral( "no such term" ) ) == nullptr );

  HelpRegistry registry;
  QStringList errors;
  TerminologyProvider::appendDescriptors( result.terms, registry, &errors );
  INFO( "descriptor errors: " << errors.join( QStringLiteral( "; " ) ).toStdString() );
  CHECK( errors.isEmpty() );
  CHECK( registry.count() == result.terms.size() );
  CHECK( registry.validateReferences().isEmpty() );
  CHECK( registry.find( termDescriptorId( QStringLiteral( "NDVI" ) ) ) != nullptr );
}

TEST_CASE( "glossary terms are reachable through the single help search index", "[i18n][terms]" )
{
  TerminologyProvider::LoadResult result = loadGlossary();
  REQUIRE( result.errors.isEmpty() );

  HelpRegistry registry;
  TerminologyProvider::appendDescriptors( result.terms, registry, nullptr );

  HelpSearchIndex index;
  index.build( registry.all() );
  REQUIRE( index.isValid() );

  const QVector<SearchHit> ndvi = index.search( QStringLiteral( "NDVI" ), 10 );
  REQUIRE_FALSE( ndvi.isEmpty() );
  bool foundTerm = false;
  for ( const SearchHit &hit : ndvi )
    if ( hit.id.startsWith( QLatin1String( "concept.term." ) ) )
      foundTerm = true;
  CHECK( foundTerm );

  const QVector<SearchHit> zh = index.search( QStringLiteral( "辐射定标" ), 10 );
  CHECK_FALSE( zh.isEmpty() );
}

TEST_CASE( "audited modules wrap user-visible strings in tr()", "[i18n][sweep]" )
{
  // A user-visible widget API called directly with a string literal — i.e.
  // not through tr() — is exactly what the D6 sweep was meant to eliminate.
  static const QRegularExpression unwrapped(
    QStringLiteral( "\\b(setText|setWindowTitle|setTitle|addItem|insertItem|addAction|"
                    "setToolTip|setStatusTip|setPlaceholderText|showMessage|setLabelText|"
                    "setTabText|setWindowTitle)\\s*\\(\\s*\"" ) );

  QStringList offenders;
  for ( const QString &relative : auditedModules() )
  {
    const QString text = readSourceTreeFile( relative );
    REQUIRE_FALSE( text.isEmpty() );
    const QStringList lines = text.split( QLatin1Char( '\n' ) );
    for ( int i = 0; i < lines.size(); ++i )
    {
      const QString line = lines.at( i );
      if ( line.trimmed().startsWith( QStringLiteral( "//" ) ) )
        continue;
      if ( unwrapped.match( line ).hasMatch() )
        offenders << QStringLiteral( "%1:%2: %3" ).arg( relative ).arg( i + 1 ).arg( line.trimmed() );
    }
  }
  INFO( "unwrapped literals: " << offenders.join( QStringLiteral( "; " ) ).toStdString() );
  CHECK( offenders.isEmpty() );
}

// ── F20 i18n drift gates (work package E) ─────────────────────────────────
//
// #983 moved the dialog help catalog to QT_TRANSLATE_NOOP("SicnuDialogHelp", …)
// storage, but the .ts was never regenerated, so the context had zero entries
// and the strings silently stayed untranslated. These guards keep the NOOP
// storage and the translation file in lockstep.

namespace {

using StringSet = QSet<QString>;

/// QT_TRANSLATE_NOOP contexts in the scanned app/help sources.
StringSet noopContexts( const QString &text )
{
  static const QRegularExpression re(
      QStringLiteral( "QT_TRANSLATE_NOOP\\s*\\(\\s*\"([^\"]+)\"" ) );
  StringSet out;
  auto it = re.globalMatch( text );
  while ( it.hasNext() )
    out.insert( it.next().captured( 1 ) );
  return out;
}

/// <context><name>X</name> names in the ts (lupdate output is stable).
StringSet tsContextNames( const QString &ts )
{
  static const QRegularExpression re(
      QStringLiteral( "<context>\\s*<name>([^<]*)</name>" ) );
  StringSet names;
  auto it = re.globalMatch( ts );
  while ( it.hasNext() )
    names.insert( it.next().captured( 1 ) );
  return names;
}

/// Decode the XML entities lupdate emits in <source>/<translation> text.
QString decodeEntities( QString text )
{
  text.replace( QStringLiteral( "&apos;" ), QChar( u'\'' ) );
  text.replace( QStringLiteral( "&quot;" ), QChar( u'"' ) );
  text.replace( QStringLiteral( "&lt;" ), QChar( u'<' ) );
  text.replace( QStringLiteral( "&gt;" ), QChar( u'>' ) );
  text.replace( QStringLiteral( "&amp;" ), QChar( u'&' ) );
  return text;
}

/// messages of one context: <source> → <translation> (entity-decoded).
QHash<QString, QPair<QString, int>> tsMessages( const QString &ts, const QString &context )
{
  QHash<QString, QPair<QString, int>> out;
  static const QRegularExpression ctxRe(
      QStringLiteral( "<context>\\s*<name>%1</name>(.*?)</context>" ).arg( QRegularExpression::escape( context ) ),
      QRegularExpression::DotMatchesEverythingOption );
  const auto ctx = ctxRe.match( ts );
  if ( !ctx.hasMatch() )
    return out;
  static const QRegularExpression msgRe(
      QStringLiteral( "<source>([^<]*)</source>\\s*<translation[^>]*>([^<]*)</translation>" ) );
  auto it = msgRe.globalMatch( ctx.captured( 1 ) );
  while ( it.hasNext() )
  {
    const auto m = it.next();
    out.insert( decodeEntities( m.captured( 1 ) ),
                { decodeEntities( m.captured( 2 ) ), 0 } );
  }
  return out;
}

/// source strings stored under one ts context → translation (may be empty).
struct TsMessage
{
  QString source;
  QString translation;
  int placeholdersInSource = 0;
  int placeholdersInTranslation = 0;
};

int countPlaceholders( const QString &text )
{
  static const QRegularExpression re( QStringLiteral( "%[1-9]" ) );
  int n = 0;
  auto it = re.globalMatch( text );
  while ( it.hasNext() )
  {
    (void) it.next();
    ++n;
  }
  return n;
}

} // namespace

TEST_CASE( "every QT_TRANSLATE_NOOP context reaches the translation file",
           "[i18n][noop]" )
{
  const QString ts = readSourceTreeFile( QStringLiteral( "resources/translations/sicnu_zh_CN.ts" ) );
  REQUIRE( !ts.isEmpty() );

  // every NOOP context in the app/help sources must exist in the ts
  StringSet contexts;
  const QStringList roots = { QStringLiteral( "src/app/dialogs/dialog_help_catalog.cpp" ),
                              QStringLiteral( "src/app/dialogs/temporal_analysis_dialog.cpp" ),
                              QStringLiteral( "src/app/classification/rs_classify_stepper_bar.cpp" ) };
  for ( const QString &relative : roots )
  {
    const QString text = readSourceTreeFile( relative );
    REQUIRE_FALSE( text.isEmpty() );
    contexts |= noopContexts( text );
  }
  REQUIRE_FALSE( contexts.isEmpty() );

  const StringSet known = tsContextNames( ts );
  REQUIRE_FALSE( known.isEmpty() );
  for ( const QString &context : contexts )
  {
    INFO( "NOOP context missing from ts: " << context.toStdString() );
    CHECK( known.contains( context ) );
  }
}

TEST_CASE( "SicnuDialogHelp NOOP strings all have non-empty translations",
           "[i18n][noop]" )
{
  const QString ts = readSourceTreeFile( QStringLiteral( "resources/translations/sicnu_zh_CN.ts" ) );
  REQUIRE( !ts.isEmpty() );

  // collect the stored source strings from the catalog
  const QString source = readSourceTreeFile( QStringLiteral( "src/app/dialogs/dialog_help_catalog.cpp" ) );
  REQUIRE_FALSE( source.isEmpty() );
  static const QRegularExpression noopRe(
      QStringLiteral( "QT_TRANSLATE_NOOP\\s*\\(\\s*\"SicnuDialogHelp\"\\s*,\\s*((?:\"(?:[^\"\\\\]|\\\\.)*\"\\s*)+)" ) );
  QStringList stored;
  auto it = noopRe.globalMatch( source );
  while ( it.hasNext() )
  {
    const QString literals = it.next().captured( 1 );
    static const QRegularExpression litRe( QStringLiteral( "\"((?:[^\"\\\\]|\\\\.)*)\"" ) );
    QString joined;
    auto lit = litRe.globalMatch( literals );
    while ( lit.hasNext() )
      joined += lit.next().captured( 1 );
    joined.replace( QStringLiteral( "\\n" ), QStringLiteral( "\n" ) );
    joined.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
    if ( !joined.isEmpty() )
      stored << joined;
  }
  REQUIRE( stored.size() >= 10 ); // scanner sanity: the catalog is NOOP'd

  // ts messages under the context
  QHash<QString, TsMessage> messages;
  const auto ctx = tsMessages( ts, QStringLiteral( "SicnuDialogHelp" ) );
  for ( auto contextIt = ctx.constBegin(); contextIt != ctx.constEnd(); ++contextIt )
  {
    TsMessage current;
    current.source = contextIt.key();
    current.translation = contextIt.value().first;
    current.placeholdersInSource = countPlaceholders( current.source );
    current.placeholdersInTranslation = countPlaceholders( current.translation );
    messages.insert( current.source, current );
  }
  REQUIRE_FALSE( messages.isEmpty() );
  INFO( "SicnuDialogHelp messages in ts: " << messages.size() );

  for ( const QString &text : stored )
  {
    INFO( "NOOP string missing from ts: " << text.left( 60 ).toStdString() );
    const bool present = messages.contains( text );
    CHECK( present );
    if ( !present )
      continue;
    const TsMessage &m = messages.value( text );
    INFO( "empty or placeholder-drifted translation for: " << text.left( 60 ).toStdString() );
    CHECK_FALSE( m.translation.trimmed().isEmpty() );
    CHECK( m.placeholdersInSource == m.placeholdersInTranslation );
  }
}
