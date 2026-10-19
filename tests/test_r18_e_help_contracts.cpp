/***************************************************************************
  tests/test_r18_e_help_contracts.cpp — R18-E E07 help/i18n contract lane.

  RED-first pins:

    T701 (H1): `severity` is the last diagnostic-content field with a
      fail-open parse — any unknown spelling (a capital "Warning", a typo)
      silently hardened into Error with zero errors reported, bypassing
      the severity-drift gates. Same fail-closed treatment `retry` already
      has: unknown vocabulary is a content error, the entry is dropped.

    T702 (H4): HelpCompact::toJsonText's last fallback truncates the JSON
      text itself (`idOnly.left(budget)`), emitting INVALID JSON when the
      budget is smaller than the id-only document — violating the
      function's own "always valid JSON" contract.

    T703 (H5): elide() (tooltip/statusTip) and toJsonText's summary cut
      still use raw QString::left, which can split a UTF-16 surrogate
      pair — #1526 fixed only truncateSummary. An astral character at the
      cut must not become a lone surrogate (U+FFFD).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "help/help_content_store.h"
#include "help/help_descriptor.h"
#include "help/help_presenter.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QString>

#include <cstring>

using namespace sicnu::help;

namespace
{

QString writeDiagnostics( const QByteArray &body )
{
  static QTemporaryDir dir;
  const QString path = dir.path() + QStringLiteral( "/diagnostics.json" );
  QFile f( path );
  REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  f.write( body );
  f.close();
  return dir.path();
}

HelpDescriptor descriptorWith( const QString &title, const QString &summary )
{
  HelpDescriptor d;
  d.id = QStringLiteral( "concept.remote_sensing" );
  d.category = QStringLiteral( "concept" );
  d.title = title;
  d.summary = summary;
  return d;
}

/// An astral character (U+1F30D GLOBE) is one UTF-16 surrogate PAIR: two
/// QChar units that must never be separated by a cut.
const QString kAstralRun = [] {
  QString run;
  for ( int i = 0; i < 40; ++i )
  {
    run += QString( QChar( 0xD83C ) );
    run += QString( QChar( 0xDF0D ) ); // U+1F30D
  }
  return run;
}();

} // namespace

// ---------------------------------------------------------------------------
// T701 — severity vocabulary is fail-closed
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T701: an unknown severity spelling is a content error, not silent Error",
           "[help][content][r18_e]" )
{
  const QString dir = writeDiagnostics( R"json([{
    "id": "diagnostic.harness.sample_failure",
    "family": "harness",
    "code": "SAMPLE_FAILURE",
    "title": "样例失败",
    "whatHappened": "发生了样例错误。",
    "whyItMatters": "会阻断后续步骤。",
    "severity": "Warning",
    "retry": "none",
    "remediation": [ "检查输入" ]
  }])json" );

  const HelpContentStore::LoadResult result = HelpContentStore::loadFromDirectory( dir );
  CHECK_FALSE( result.errors.isEmpty() );
  bool named = false;
  for ( const QString &error : result.errors )
    if ( error.contains( QStringLiteral( "severity" ) ) )
      named = true;
  CHECK( named );
}

TEST_CASE( "r18e T701b: the closed vocabulary still loads cleanly",
           "[help][content][r18_e]" )
{
  for ( const char *severity : { "info", "warning", "error" } )
  {
    const QByteArray body = QByteArray( R"json([{
      "id": "diagnostic.harness.sample_failure",
      "family": "harness",
      "code": "SAMPLE_FAILURE",
      "title": "样例失败",
      "whatHappened": "发生了样例错误。",
      "whyItMatters": "会阻断后续步骤。",
      "severity": ")json" ) + severity + QByteArray( R"json(",
      "retry": "none",
      "remediation": [ "检查输入" ]
    }])json" );
    const QString dir = writeDiagnostics( body );
    const HelpContentStore::LoadResult result = HelpContentStore::loadFromDirectory( dir );
    INFO( severity << " -> " << result.errors.join( QStringLiteral( ";" ) ).toStdString() );
    CHECK( result.errors.isEmpty() );
  }
}

// ---------------------------------------------------------------------------
// T702 — toJsonText always emits valid JSON
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T702: an adversarially small budget still yields valid JSON",
           "[help][presenter][r18_e]" )
{
  const HelpDescriptor d = descriptorWith( QStringLiteral( "标题" ),
                                           QStringLiteral( "一段较长的摘要文本用于压缩" ) );
  for ( int budget = 2; budget <= 24; ++budget )
  {
    const QString text = HelpCompact::toJsonText( d, budget );
    INFO( "budget=" << budget << " text=" << text.toStdString() );
    CHECK_FALSE( text.isEmpty() );
    const QJsonDocument parsed = QJsonDocument::fromJson( text.toUtf8() );
    CHECK( parsed.isObject() );
  }
}

// ---------------------------------------------------------------------------
// T703 — astral characters survive every cut
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T703: a cut at a surrogate pair boundary keeps the pair whole",
           "[help][presenter][r18_e]" )
{
  const HelpDescriptor d = descriptorWith( QStringLiteral( "标题" ), kAstralRun );

  const QString tip = HelpPresenter::tooltip( d, 21 );
  CHECK_FALSE( tip.isEmpty() );
  CHECK_FALSE( tip.contains( QChar( 0xFFFD ) ) );
  // A lone (unpaired) trailing high surrogate is the split itself.
  const QString::const_iterator last = tip.cend() - 1;
  CHECK_FALSE( last->isHighSurrogate() );

  const QString status = HelpPresenter::statusTip( d );
  CHECK_FALSE( status.contains( QChar( 0xFFFD ) ) );

  // The compact summary's elision path also cuts astral runs.
  const QString summary = HelpCompact::summary( d, 20 );
  CHECK_FALSE( summary.contains( QChar( 0xFFFD ) ) );

  const QString json = HelpCompact::toJsonText( d, 400 );
  CHECK_FALSE( json.contains( QChar( 0xFFFD ) ) );
  const QJsonDocument parsed = QJsonDocument::fromJson( json.toUtf8() );
  CHECK( parsed.isObject() );
}
