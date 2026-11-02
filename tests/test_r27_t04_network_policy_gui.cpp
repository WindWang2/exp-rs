// tests/test_r27_t04_network_policy_gui.cpp — R27-T04 (#1765)
//
// Contract under test: the GUI entry point carries the same process-wide
// private-network policy as the CLI/worker entry points, because the GUI
// opens remote COGs in-process (STAC browser /vsicurl/ loads). Two pins:
//   1. Behavioral: with the policy engaged (exactly what src/app/main.cpp
//      now does), a raw /vsicurl/ open of a loopback address is refused
//      before any network I/O — the SSRF bypass the issue flags.
//   2. Structural (source contract): main.cpp engages the policy before
//      QgsApplication construction, with the documented env opt-out.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QtTest>

#include <cpl_vsi.h>
#include <gdal.h>

#include "geospatial/remote/process_network_policy.h"

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_network_policy_gui";
  char *fake_argv[] = { fake_argv0, nullptr };

  QCoreApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
      return new QCoreApplication( fake_argc, fake_argv );
    return QCoreApplication::instance();
  }

  const char *kMainCpp = CMAKE_SOURCE_DIR "/src/app/main.cpp";
} // namespace

TEST_CASE( "Engaged policy refuses raw /vsicurl/ loopback opens (#1765)",
           "[app][network][ssrf][r27t04]" )
{
  ensureApp();
  GDALAllRegister();

  // Exactly what src/app/main.cpp runs at startup now.
  sicnu::geo::network_policy::engageProcessPrivateNetworkBlocking();
  REQUIRE( sicnu::geo::network_policy::processPrivateNetworkBlockingEngaged() );

  // A loopback host is refused by the host check before any connect —
  // deterministic, no server needed. On HEAD the GUI never engaged the
  // policy, so this open reached raw libcurl (the SSRF bypass).
  VSILFILE *f = VSIFOpenL( "/vsicurl/http://127.0.0.1:9/x.tif", "rb" );
  CHECK( f == nullptr );
  if ( f )
    VSIFCloseL( f );

  VSIStatBufL st;
  CHECK( VSIStatL( "/vsicurl/http://127.0.0.1:9/x.tif", &st ) != 0 );

  f = VSIFOpenL( "/vsicurl_streaming/http://[::1]:9/x.tif", "rb" );
  CHECK( f == nullptr );
  if ( f )
    VSIFCloseL( f );
}

TEST_CASE( "GUI entry engages the private-network policy before init (#1765, source contract)",
           "[app][network][ssrf][r27t04][source]" )
{
  ensureApp();
  QFile source( QString::fromLatin1( kMainCpp ) );
  REQUIRE( source.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString text = QString::fromUtf8( source.readAll() );

  const qsizetype engagePos = text.indexOf(
    QStringLiteral( "engageProcessPrivateNetworkBlocking();" ) );
  REQUIRE( engagePos >= 0 ); // RED on HEAD: the GUI entry never engages

  // The engage must precede QgsApplication construction (any earlier open
  // would run unguarded), and the opt-out env must be documented in code.
  const qsizetype appPos = text.indexOf( QStringLiteral( "new QgsApplication" ) );
  REQUIRE( appPos > engagePos );
  CHECK( text.indexOf( QStringLiteral( "SICNU_ALLOW_PRIVATE_NETWORK" ), 0 ) >= 0 );
}
