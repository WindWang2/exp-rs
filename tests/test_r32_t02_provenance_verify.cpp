// tests/test_r32_t02_provenance_verify.cpp — R32-T02 WP06 (#2089).
//
// The public contract of verifyProductProvenance / verifyProductAgainstModel
// is "Never writes. Never throws." (provenance_verify.h). #2089: jsoncpp's
// isMember()/operator[] throw Json::LogicError on non-object documents, and
// the schema-rejection branch re-entered isMember("schema") to format its
// own message — an array/scalar/string/bool sidecar escaped the contract as
// an exception. This suite drives the REAL typed API over every adversarial
// sidecar shape: a Catch2 CHECK completing at all is the no-throw evidence
// (an escaping Json::LogicError fails the case with the exception text),
// and the sidecar bytes are compared before/after to prove zero writes.
// Runs in every OpenCV-enabled build (no provider dependency).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/runtime/provenance_verify.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <string>

namespace {

using sicnu::operators::runtime::ProvenanceVerdict;
using sicnu::operators::runtime::provenanceSidecarPath;
using sicnu::operators::runtime::verifyProductProvenance;

QByteArray fileBytes( const QString &path )
{
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return {};
  return f.readAll();
}

QByteArray sha( const QByteArray &bytes )
{
  return QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 );
}

} // namespace

TEST_CASE( "R32-T02 WP06: verifier never throws and types every non-object sidecar",
           "[r32t02][wp06][provenance]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString product =
    sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 1, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "product.tif" ) ) );
  REQUIRE_FALSE( product.isEmpty() );
  const std::string productPath = product.toStdString();
  const QString sidecarPath = QString::fromStdString( provenanceSidecarPath( productPath ) );

  const auto writeSidecar = [ & ]( const std::string &text ) {
    QFile sidecar( sidecarPath );
    REQUIRE( sidecar.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    sidecar.write( text.data(), static_cast<qint64>( text.size() ) );
    sidecar.close();
  };

  // #2089 repro set: every NON-OBJECT document. Before the fix each of
  // array / string / number / bool escaped as an uncaught Json::LogicError
  // out of verifyProductProvenance (isMember("schema") asserts objectValue);
  // null already returned UnsupportedSchema. The verdict below must be the
  // typed UnsupportedSchema with the JSON type named in the detail.
  const struct
  {
    const char *json;
    const char *typeName;
  } nonObjectDocs[] = {
    { R"([1, 2, 3])", "array" },
    { R"("just a string")", "string" },
    { R"(42)", "number" },
    { R"(true)", "boolean" },
    { R"(null)", "null" },
  };
  for ( const auto &doc : nonObjectDocs )
  {
    CAPTURE( doc.json );
    writeSidecar( doc.json );
    const QByteArray before = fileBytes( sidecarPath );
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::UnsupportedSchema );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( doc.typeName ) );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "exp-rs-prov/1" ) );
    // Zero writes: the sidecar is byte-identical after verification.
    CHECK( fileBytes( sidecarPath ) == before );
  }

  // Object without a schema member: typed "<absent>" detail, no throw.
  writeSidecar( R"({ "model": { "identity_tag": "m@1" } })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::UnsupportedSchema );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "<absent>" ) );
  }

  // Object with a NON-STRING schema member: same typed branch (a number is
  // not a schema name), detail stays "<absent>", no operator[] type trap.
  writeSidecar( R"({ "schema": 42 })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::UnsupportedSchema );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "<absent>" ) );
  }

  // Object with a WRONG schema string: the value itself is reported.
  writeSidecar( R"({ "schema": "some-other-prov/9" })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::UnsupportedSchema );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "some-other-prov/9" ) );
  }
}

TEST_CASE( "R32-T02 WP06: wrong-typed recorded grid fields stay typed verdicts",
           "[r32t02][wp06][provenance]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString product =
    sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 1, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "product.tif" ) ) );
  REQUIRE_FALSE( product.isEmpty() );
  const std::string productPath = product.toStdString();
  const QString sidecarPath = QString::fromStdString( provenanceSidecarPath( productPath ) );

  const auto writeSidecar = [ & ]( const std::string &text ) {
    QFile sidecar( sidecarPath );
    REQUIRE( sidecar.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    sidecar.write( text.data(), static_cast<qint64>( text.size() ) );
    sidecar.close();
  };

  // Schema-valid document whose recorded output.width is an ARRAY: the
  // grid-check tri-state must report MalformedSidecar (wrong-typed field),
  // never throw and never confuse it with a real grid mismatch.
  writeSidecar( R"({ "schema": "exp-rs-prov/1", "output": { "width": [8], "height": 8, "bands": 1 } })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::MalformedSidecar );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "width" ) );
  }

  // A matching recorded grid over the real raster: no false alarm (the
  // output block checks agree), verdict falls through to Ok.
  writeSidecar( R"({ "schema": "exp-rs-prov/1", "output": { "width": 8, "height": 8, "bands": 1 } })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::Ok );
  }

  // A contradicting recorded width: GridMismatch with the numbers named.
  writeSidecar( R"({ "schema": "exp-rs-prov/1", "output": { "width": 16, "height": 8, "bands": 1 } })" );
  {
    const ProvenanceVerdict verdict = verifyProductProvenance( productPath );
    CHECK( verdict.state == ProvenanceVerdict::State::GridMismatch );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "16" ) );
    CHECK_THAT( verdict.detail, Catch::Matchers::ContainsSubstring( "8" ) );
  }
}
