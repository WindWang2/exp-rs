/***************************************************************************
 * test_help_commands_schema.cpp — Track 17 R4 (WP-D)
 *
 * Schema-level gate for data/help/commands.json that the corpus-shape gate
 * in test_help_integrity_12 deliberately does not provide: it only asserts
 * parse + id presence/uniqueness across the whole corpus. The command
 * glossary additionally carries per-entry fields consumers rely on
 * (purpose/keywords for search, prerequisites/related arrays for the help
 * graph), and the bidirectional registry diff lives in test_help_coverage.
 *
 * This file pins, for commands.json only:
 *   1. entry floor (anti-silent-emptying; floor = the pre-round2 audit
 *      baseline of 76, actual 81 after closing the 5 gaps),
 *   2. per-entry schema (command.* prefix, unique ids, non-empty purpose,
 *      non-empty keyword array, array-typed optional fields),
 *   3. EOL independence: the same bytes re-parsed after an LF→CRLF
 *      rewrite must yield the identical id set (mirrors the lab-pack
 *      EOL-independent verification precedent from #1336).
 *
 * Deliberately does NOT go through HelpContentStore: tolerance in the
 * loader is what hid E-15.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace {

QJsonDocument parseBytes( const QByteArray &bytes, QJsonParseError *error )
{
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson( bytes, &pe );
  if ( error )
    *error = pe;
  return doc;
}

QJsonDocument loadCommands( QJsonParseError *error )
{
  QFile f( QStringLiteral( CMAKE_SOURCE_DIR "/data/help/commands.json" ) );
  REQUIRE( f.open( QIODevice::ReadOnly ) );
  return parseBytes( f.readAll(), error );
}

} // namespace

TEST_CASE( "commands.json keeps its entry floor", "[help][commands][schema]" )
{
  QJsonParseError pe;
  const QJsonDocument doc = loadCommands( &pe );
  INFO( "parse error: " << pe.errorString().toStdString() );
  REQUIRE( pe.error == QJsonParseError::NoError );
  REQUIRE( doc.isArray() );
  // Floor = the 76-entry audit baseline this track started from (a silent
  // truncation to the pre-audit corpus must not pass unnoticed).
  CHECK( doc.array().size() >= 76 );
}

TEST_CASE( "commands.json entries satisfy the glossary schema",
           "[help][commands][schema]" )
{
  QJsonParseError pe;
  const QJsonDocument doc = loadCommands( &pe );
  REQUIRE( pe.error == QJsonParseError::NoError );
  REQUIRE( doc.isArray() );

  QSet<QString> seen;
  QStringList problems;
  int i = 0;
  for ( const QJsonValue &v : doc.array() )
  {
    ++i;
    if ( !v.isObject() )
    {
      problems << QStringLiteral( "entry %1 is not an object" ).arg( i );
      continue;
    }
    const QJsonObject e = v.toObject();
    const QString id = e.value( QStringLiteral( "id" ) ).toString();
    if ( !id.startsWith( QLatin1String( "command." ) ) )
      problems << QStringLiteral( "entry %1 id '%2' lacks the command. prefix" ).arg( i ).arg( id );
    if ( seen.contains( id ) )
      problems << QStringLiteral( "duplicate id '%1'" ).arg( id );
    seen.insert( id );
    if ( e.value( QStringLiteral( "purpose" ) ).toString().trimmed().isEmpty() )
      problems << QStringLiteral( "%1 has an empty purpose" ).arg( id );
    const QJsonValue kw = e.value( QStringLiteral( "keywords" ) );
    if ( !kw.isArray() || kw.toArray().isEmpty() )
      problems << QStringLiteral( "%1 has no keywords" ).arg( id );
    for ( const QString &field : { QStringLiteral( "prerequisites" ), QStringLiteral( "related" ) } )
    {
      const QJsonValue fv = e.value( field );
      if ( !fv.isUndefined() && !fv.isArray() )
        problems << QStringLiteral( "%1 field %2 is not an array" ).arg( id, field );
    }
  }
  for ( const QString &p : problems )
    UNSCOPED_INFO( "commands.json schema defect: " << p.toStdString() );
  CHECK( problems.isEmpty() );
}

TEST_CASE( "commands.json parses identically under CRLF line endings",
           "[help][commands][schema][eol]" )
{
  QFile f( QStringLiteral( CMAKE_SOURCE_DIR "/data/help/commands.json" ) );
  REQUIRE( f.open( QIODevice::ReadOnly ) );
  const QByteArray bytes = f.readAll();

  QJsonParseError pe1, pe2;
  const QJsonDocument asIs = parseBytes( bytes, &pe1 );
  REQUIRE( pe1.error == QJsonParseError::NoError );

  // Simulate a Windows checkout: rewrite every LF to CRLF, then parse.
  QByteArray crlf = bytes;
  crlf.replace( "\n", "\r\n" );
  const QJsonDocument asCrlf = parseBytes( crlf, &pe2 );
  REQUIRE( pe2.error == QJsonParseError::NoError );

  QSet<QString> idsAsIs, idsCrlf;
  for ( const QJsonValue &v : asIs.array() )
    idsAsIs.insert( v.toObject().value( QStringLiteral( "id" ) ).toString() );
  for ( const QJsonValue &v : asCrlf.array() )
    idsCrlf.insert( v.toObject().value( QStringLiteral( "id" ) ).toString() );
  CHECK( idsAsIs == idsCrlf );
  CHECK( idsAsIs.size() == asIs.array().size() );
}
