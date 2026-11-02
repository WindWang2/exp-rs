// tests/test_r27_t04_pipeline_json.cpp — R27-T04 (#1793)
//
// Contract under test: opening a user-supplied pipeline definition JSON
// must survive adversarial input. The reader pins jsoncpp's stackLimit
// (#1607 discipline) and catches the reader's throw: a deeply nested
// document yields a user-facing parse error, never an uncaught exception
// aborting the process inside a Qt slot.
#include <catch2/catch_test_macros.hpp>

#include "app/workflow/pipeline_editor_dock.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <json/json.h>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_pipeline_json";
  char *fake_argv[] = { fake_argv0, nullptr };

  QCoreApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
      return new QCoreApplication( fake_argc, fake_argv );
    return QCoreApplication::instance();
  }

  QString writeTemp( const QString &name, const QByteArray &bytes )
  {
    static QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( name );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( f.write( bytes ) == bytes.size() );
    f.close();
    return path;
  }
} // namespace

TEST_CASE( "Pipeline JSON open refuses deep nesting instead of aborting (#1793)",
           "[workflow][pipeline][json][r27t04]" )
{
  ensureApp();

  // --- well-formed minimal document parses ---------------------------
  {
    const QString path = writeTemp( QStringLiteral( "ok.json" ),
                                    R"({"name":"demo","steps":[]})" );
    Json::Value root;
    QString err;
    CHECK( sicnu::workflow::gui::PipelineEditorDock::readPipelineDefinitionFile(
      path, root, &err ) );
    CHECK( root.isObject() );
  }

  // --- 100k-deep nesting: parse error, no crash ----------------------
  {
    const QByteArray deep =
      QByteArray( 100000, '[' ) + QByteArray( 100000, ']' );
    const QString path = writeTemp( QStringLiteral( "deep.json" ), deep );
    Json::Value root;
    QString err;
    // RED on HEAD: jsoncpp's unbounded reader threw std::runtime_error
    // ("Exceeded stackLimit" / stack overflow) out of the Qt slot.
    CHECK_FALSE( sicnu::workflow::gui::PipelineEditorDock::readPipelineDefinitionFile(
      path, root, &err ) );
    CHECK( !err.isEmpty() );
    CHECK( root.isNull() );
  }

  // --- deep nesting inside an object value ----------------------------
  {
    const QByteArray deepObject =
      QByteArray( "{\"k\":" ) + QByteArray( 100000, '[' ) + QByteArray( 100000, ']' ) + "}";
    const QString path = writeTemp( QStringLiteral( "deep2.json" ), deepObject );
    Json::Value root;
    QString err;
    CHECK_FALSE( sicnu::workflow::gui::PipelineEditorDock::readPipelineDefinitionFile(
      path, root, &err ) );
    CHECK( !err.isEmpty() );
  }

  // --- garbage: parse error, not a throw ------------------------------
  {
    const QString path = writeTemp( QStringLiteral( "garbage.json" ),
                                    QByteArray::fromHex( "00ff108842" ) );
    Json::Value root;
    QString err;
    CHECK_FALSE( sicnu::workflow::gui::PipelineEditorDock::readPipelineDefinitionFile(
      path, root, &err ) );
    CHECK( !err.isEmpty() );
  }
}
