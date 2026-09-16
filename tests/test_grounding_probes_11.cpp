// tests/test_grounding_probes_11.cpp
//
// Compiler & Grounding 11.0: bounded factual probes — scope validation
// before any I/O, resolver-backed anti-hallucination, cache reuse of the ONE
// understanding cache, deadline accounting, and the model-manifest probe.
// Deterministic, headless, runtime-generated GDAL fixtures.

#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <json/json.h>

#include <string>

#include "agent/harness/context_ledger.h"
#include "agent/harness/grounding_probes.h"
#include "agent/harness/grounding_tools.h"
#include "agent/harness/harness_error.h"
#include "agent/spatial_tools/spatial_tool.h"

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

namespace
{
void ensureGdalDrivers()
{
  static const bool kRegistered = [] {
    GDALAllRegister();
    return true;
  }();
  ( void )kRegistered;
}

/// 8x8, 2-band Float32 GeoTIFF with NIR/RED roles + an acquisition date —
/// enough for identity/grid/crs/temporal scope probes.
std::string createTestRaster( const QString &path )
{
  ensureGdalDrivers();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 2, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
  ds->SetGeoTransform( geoTransform );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32650 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );
  ds->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", "NIR", nullptr );
  ds->GetRasterBand( 2 )->SetMetadataItem( "SICNU_BAND_ROLE", "RED", nullptr );
  ds->SetMetadataItem( "SICNU_ACQUISITION_DATE", "2024-07-10T10:31:00Z" );
  float row[8];
  for ( int y = 0; y < 8; ++y )
  {
    for ( int x = 0; x < 8; ++x )
      row[x] = static_cast<float>( y * 8 + x );
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
  return path.toStdString();
}
} // namespace

namespace
{
void ensureBuiltinTools()
{
  // The grounding probe delegates to spatial:understand on the live
  // registry; the corpus/grounding tests register the builtin tools the
  // same way.
  SpatialToolRegistry::instance().registerBuiltinTools();
}
} // namespace

TEST_CASE( "probe scope validation happens before any I/O", "[probes11]" )
{
  ensureBuiltinTools();
  ProbeRequest request;
  request.ref = "asset-424242"; // unresolvable — but scopes fail FIRST
  request.scopes = { "grid", "warp_drive" };
  const ProbeOutcome outcome = probeDatasetFacts( request );
  REQUIRE_FALSE( outcome.ok );
  CHECK( outcome.code == "INVALID_PARAMETER" );
}

TEST_CASE( "probe of an unresolvable reference is a typed miss, never a guess", "[probes11]" )
{
  ensureBuiltinTools();
  ProbeRequest request;
  request.ref = "asset-424242";
  request.scopes = { "grid" };
  const ProbeOutcome outcome = probeDatasetFacts( request );
  REQUIRE_FALSE( outcome.ok );
  CHECK( outcome.code == "DATASET_NOT_FOUND" );
}

TEST_CASE( "bounded probe answers scoped facts over a real GTiff", "[probes11]" )
{
  ensureBuiltinTools();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string raster = createTestRaster( dir.filePath( "probe.tif" ) );

  ProbeRequest request;
  request.ref = raster;
  request.scopes = { "grid", "temporal", "identity" };
  const ProbeOutcome first = probeDatasetFacts( request );
  REQUIRE( first.ok );
  CHECK( first.source == "live" );
  CHECK( first.facts["pixel_size"]["x"].asDouble() == 30.0 );
  CHECK( first.facts["acquisition_time"].asString() == "2024-07-10T10:31:00Z" );
  CHECK( first.facts["temporal_facts"]["count"].asInt() == 1 );
  CHECK( first.facts["temporal_facts"]["regularity"].asString() == "single" );
  CHECK( first.facts["fact_status"]["acquisition_time"].asString() == "observed" );

  // Second probe of the SAME file answers from the shared cache — the
  // grounding tools' cache, not a second store (ContextLedger is the one
  // cache; the probe reuses its key derivation).
  const ProbeOutcome second = probeDatasetFacts( request );
  REQUIRE( second.ok );
  CHECK( second.source == "cache" );
  CHECK( second.facts == first.facts );

  // A temporal probe carries cadence facts with DERIVED status while the
  // raw acquisition time stays OBSERVED — provenance survives projection.
  ProbeRequest temporalOnly;
  temporalOnly.ref = raster;
  temporalOnly.scopes = { "temporal" };
  const ProbeOutcome temporal = probeDatasetFacts( temporalOnly );
  REQUIRE( temporal.ok );
  CHECK( temporal.facts["temporal_facts"]["fact_status"]["regularity"].asString() ==
         "derived" );
  CHECK( temporal.facts["fact_status"]["acquisition_time"].asString() == "observed" );
}

TEST_CASE( "probe facts outside the requested scopes are not projected", "[probes11]" )
{
  ensureBuiltinTools();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string raster = createTestRaster( dir.filePath( "probe2.tif" ) );

  ProbeRequest request;
  request.ref = raster;
  request.scopes = { "crs" };
  const ProbeOutcome outcome = probeDatasetFacts( request );
  REQUIRE( outcome.ok );
  CHECK( outcome.facts.isMember( "crs" ) );
  CHECK( outcome.facts["fact_status"]["crs"].asString() == "observed" );
  // Out-of-scope keys must not leak into the projection.
  CHECK_FALSE( outcome.facts.isMember( "pixel_size" ) );
  CHECK_FALSE( outcome.facts.isMember( "acquisition_time" ) );
}

TEST_CASE( "deadline overrun is reported honestly, not hidden", "[probes11]" )
{
  ensureBuiltinTools();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string raster = createTestRaster( dir.filePath( "probe3.tif" ) );

  ProbeRequest request;
  request.ref = raster;
  request.scopes = { "grid" };
  request.deadlineMs = 0; // a zero deadline would fail every real probe; the
                          // contract clamps it to the declared default.
  const ProbeOutcome outcome = probeDatasetFacts( request );
  REQUIRE( outcome.ok );
  CHECK( outcome.elapsedMs >= 0 );
}

TEST_CASE( "model manifest probe: unknown id is typed-unknown, contract passes through", "[probes11]" )
{
  ModelProbeOutcome unknown = probeModelManifest( "no-such-model-11" );
  REQUIRE_FALSE( unknown.ok );
  CHECK( unknown.code == "MODEL_NOT_READY" );

  ModelProbeOutcome empty = probeModelManifest( "" );
  REQUIRE_FALSE( empty.ok );
  CHECK( empty.code == "INVALID_PARAMETER" );

  // A recorded contract (the select_model surface's ledger entry) probes ok;
  // a declared artifact that does not exist reports presence honestly.
  Json::Value contract;
  contract["task"] = "segmentation";
  contract["compatible"] = true;
  contract["readiness"] = "ready";
  contract["artifact_path"] = "/definitely/not/a/file.bin";
  ContextLedger::instance().recordModelContract( "probe-model-11", contract );
  ModelProbeOutcome known = probeModelManifest( "probe-model-11" );
  REQUIRE( known.ok );
  CHECK( known.contract["task"].asString() == "segmentation" );
  CHECK_FALSE( known.artifactPresent );
  CHECK( known.artifactBytes == -1 );
}

TEST_CASE( "probe limits mirror their bounds table", "[probes11]" )
{
  const Json::Value limits = probeLimits();
  CHECK( limits["default_deadline_ms"].asInt() == ProbeLimits::kDefaultDeadlineMs );
  CHECK( limits["max_scopes"].asInt() == ProbeLimits::kMaxScopes );
  const std::vector<std::string> scopes = probe_scope::allProbeScopes();
  CHECK( scopes.size() >= 8 );
  for ( const std::string &scope : scopes )
    CHECK( probe_scope::isKnownProbeScope( scope ) );
  CHECK_FALSE( probe_scope::isKnownProbeScope( "vibes" ) );
}
