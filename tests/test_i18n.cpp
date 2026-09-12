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
#include <QString>
#include <QXmlStreamReader>

#include <QRegularExpression>

#include "help/help_registry.h"
#include "help/help_search_index.h"
#include "help/terminology_provider.h"

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
          if ( xml.readElementText().trimmed().isEmpty() )
            ++empty;
        }
      }
    }
  }
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
