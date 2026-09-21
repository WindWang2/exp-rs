// Shipped algorithm_meta sidecars must agree with the in-code
// AlgorithmDescriptor — the descriptor is the single source of truth for
// capability facts (#707, #729). This test enforces:
// 1. Exact bidirectional membership: descriptor task-declaring set == shipped sidecars
// 2. Byte-for-byte reproducibility: generateCatalog output == disk files
// 3. No unresolvable shipped IDs (hard failure, not advisory warning)
// 4. Clean exportCatalog roundtrip to temporary directory

#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <set>

namespace {

std::string lfOnly( std::string text )
{
  std::string out;
  out.reserve( text.size() );
  for ( char c : text )
    if ( c != '\r' )
      out.push_back( c );
  return out;
}

} // namespace

TEST_CASE( "Shipped algorithm_meta sidecars agree with the registry descriptors (#707, #729)",
           "[agent][spatial][meta][drift]" )
{
    // Trigger the call_once chain so every built-in operator is registered.
    sicnu::operators::RSOperatorRegistry::instance();
    sicnu::operators::rs::installRsOperatorProvider();
    sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

    auto &store = sicnu::processing::AlgorithmMetaStore::instance();
    REQUIRE( store.loadDefaults() >= 6 );

    const auto descriptors =
        sicnu::processing::AtomicAlgorithmRegistry::instance().listDescriptors();
    REQUIRE_FALSE( descriptors.empty() );

    const auto expectedCatalog =
        sicnu::processing::AlgorithmMetaStore::generateCatalog( descriptors );

    // Baseline truth: exactly 32 algorithms declare a taskFamily in code
    // (7 from the Platform 3.0/4.0 tracks; Foundation 5.0 added the raster
    // spatial / spectral detection / SAR / temporal-monitor / terrain-flow /
    // topographic-correction families; rs:detect|segment|embedding declared
    // tasks without shipped sidecars at the Foundation 5.0 baseline and are
    // now exported like the rest; Scientific Processing 8.0 added
    // rs:sar_geocode, rs:sar_temporal_stats, rs:rasterize, rs:zonal_stats).
    // Advanced InSAR 11.0 added rs:sar_remove_topographic_phase,
    // rs:sar_coregister_local, rs:sar_pair_network, rs:sar_network_inversion.
    // Platform 10.0 added rs:classify, rs:change, rs:regress (task adapters
    // over the model execution seam).
    // Spectral Intelligence 11.0 added rs:local_rx_anomaly (anomaly-detection),
    // rs:sparse_unmixing (unmixing), rs:spectral_similarity (classification),
    // rs:endmember_analysis (endmember-analysis); also reconciles upstream drift (D14 SAR operators rs:sar_coregister/displacement/interferogram/phase_filter had no sidecars; D16 temporal sidecars for operators that no longer declare task families removed).
    // Spectral Intelligence 12.0 added rs:cem_detection and
    // rs:spectral_spatial_fuse (target-detection), and reconciles drift that
    // had accumulated on master (the gate was red at adf8f989: pin 43 vs live
    // catalog 53): rs:brdf_normalization / rs:solar_geometry /
    // rs:radiometric_qa (Radiometric Physics 11.0), rs:terrain_landform /
    // rs:terrain_solar / rs:terrain_viewshed (F16) had no shipped sidecars,
    // and four rs:temporal_* sidecars whose operators no longer declare task
    // families were still on disk (the D16 removal never landed on disk).
    // Temporal Phenology 12.0 adds rs:temporal_sar_fusion (capability sidecar
    // + contract row only — no task family, so it stays out of this catalog).
    REQUIRE( expectedCatalog.size() == 55 ); // 51 master-sidecar set at adf8f989 + 6 missing exports + 4 stale removals + 2 Spectral Intelligence 12.0

    const QString metaDir =
        sicnu::processing::resolveRuntimeDataPath( QStringLiteral( "data/processing/algorithm_meta" ) );
    const QDir dir( metaDir );
    REQUIRE( dir.exists() );

    // 1. Directory hygiene: verify only expected sidecar JSONs and README.md exist
    const QStringList diskJsonFiles = dir.entryList( QStringList{ QStringLiteral( "*.json" ) }, QDir::Files, QDir::Name );
    REQUIRE( diskJsonFiles.size() == static_cast<int>( expectedCatalog.size() ) );

    for ( const QString &entryName : dir.entryList( QDir::Files | QDir::NoDotAndDotDot ) )
    {
        if ( entryName != QStringLiteral( "README.md" ) )
        {
            CHECK( entryName.endsWith( QStringLiteral( ".json" ) ) );
        }
    }

    // 2. Exact bidirectional membership & byte-for-byte reproducibility
    for ( const auto &[filename, expectedContent] : expectedCatalog )
    {
        DYNAMIC_SECTION( "reproducibility: " << filename )
        {
            const QString filePath = dir.filePath( QString::fromStdString( filename ) );
            QFile file( filePath );
            INFO( "Expected sidecar file must exist: " << filePath.toStdString() );
            REQUIRE( file.exists() );
            REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
            const std::string diskContent = file.readAll().toStdString();
            file.close();

            INFO( "Sidecar content on disk drifted from in-code descriptor generation." );
            INFO( "Regenerate with: sicnu_geo_rs_cli --export-catalog data/processing/algorithm_meta" );
            // Line-ending-insensitive on both sides: Windows checkouts with
            // core.autocrlf materialize CRLF while the generator emits LF; the
            // gate is byte-for-byte content equality, not checkout normalization.
            CHECK( lfOnly( diskContent ) == lfOnly( expectedContent ) );
        }
    }

    // 3. Hard failure on unresolvable sidecar IDs and capability drift
    for ( const auto &entry : store.entries() )
    {
        DYNAMIC_SECTION( "resolution: " << entry.id )
        {
            auto adapter =
                sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( entry.id );
            if ( !adapter )
            {
                auto op = sicnu::operators::RSOperatorRegistry::instance().create( entry.id );
                if ( op )
                {
                    adapter = std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) );
                    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( adapter );
                }
            }

            INFO( "Sidecar id must resolve in AtomicAlgorithmRegistry: " << entry.id );
            REQUIRE( adapter != nullptr );

            const auto &descriptor = adapter->descriptor();
            std::vector<std::string> drift;
            const auto resolved = store.resolveAgainstDescriptor( entry.id, descriptor.agentMetadata, &drift );
            REQUIRE( resolved.has_value() );
            for ( const auto &d : drift )
                INFO( "capability drift: " << d );
            CHECK( drift.empty() );
        }
    }

    // 4. Export-catalog roundtrip verification to a temporary directory
    SECTION( "exportCatalog produces byte-identical files in temp directory" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        std::string exportErr;
        const int written = sicnu::processing::AlgorithmMetaStore::exportCatalog(
            tempDir.path().toStdString(), descriptors, &exportErr );
        REQUIRE( written == static_cast<int>( expectedCatalog.size() ) );
        REQUIRE( exportErr.empty() );

        const QDir tempQDir( tempDir.path() );
        for ( const auto &[filename, expectedContent] : expectedCatalog )
        {
            QFile tempFile( tempQDir.filePath( QString::fromStdString( filename ) ) );
            REQUIRE( tempFile.exists() );
            REQUIRE( tempFile.open( QIODevice::ReadOnly | QIODevice::Text ) );
            const std::string generated = tempFile.readAll().toStdString();
            CHECK( generated == expectedContent );
        }
    }
}
