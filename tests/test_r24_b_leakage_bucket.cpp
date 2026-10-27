// tests/test_r24_b_leakage_bucket.cpp — R24-B (WP-B4):
//   DATA-P2a: leakage_audit's bucketOf cast qint64(floor(x/cell)) is
//   undefined behavior for quotients beyond the integer range. An
//   extreme-but-parseable WKT coordinate (1e19 with a 1.0 cell) wrapped to
//   INT64_MIN on x64, collapsing extreme samples into one garbage bucket —
//   the spatial pair checks then silently NEVER compared them and the
//   report said "clean" with the checks listed as run. Fixed by the #875
///  split.cpp guard (checked bucket + counted exclusion).
//   DATA-P2b: dataset_store::open treats a FAILED schema-gate read as a
//   fresh store and stamps its own schema_version over a possibly-NEWER
//   stamp. Now the gate read is tri-state and a failed read refuses open.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/leakage_audit.h"
#include "dataset/split.h"

#include <QFile>
#include <QTemporaryDir>

#include <sqlite3.h>

#include <cmath>
#include <limits>

using namespace sicnu::dataset;

namespace {

AuditSample sampleWithBounds( const QString &id, SplitRole role, double minX, double minY,
                              double maxX, double maxY )
{
    SplitInput input;
    input.sampleId = id;
    input.classCode = QStringLiteral( "a" );
    AuditSample sample;
    sample.input = input;
    sample.role = role;
    sample.fold = -1;
    sample.input.validBounds = true;
    sample.input.minX = minX;
    sample.input.minY = minY;
    sample.input.maxX = maxX;
    sample.input.maxY = maxY;
    return sample;
}

} // namespace

TEST_CASE( "extreme coordinates cannot UB the leakage grid and are counted "
           "as unbucketed (R24-B)",
           "[r24_b][dataset][leakage][bucket]" )
{
    // distance check with a 1.0 cell: centers at ±1e19 overflow qint64.
    LeakageAuditConfig config;
    config.distanceThreshold = 1.0;

    const AuditSample train = sampleWithBounds( QStringLiteral( "train-extreme" ),
                                                SplitRole::Train, 0.0, 0.0, 10.0, 10.0 );
    AuditSample test = sampleWithBounds( QStringLiteral( "test-extreme" ), SplitRole::Test,
                                         0.0, 0.0, 10.0, 10.0 );
    // Center (1e20, 1e20): the overlap check's default-on state sizes the
    // x-cell at 2·maxHalfWidth = 10, so the guard fires only beyond
    // 2^62·10 ≈ 4.6e19 — 1e20 keeps the quotient at 1e19, comfortably past
    // the cap (the old qint64 cast was UB; MSVC saturates to INT64_MIN so
    // all such samples collapsed into one bucket that pair scans skipped).
    test.input.minX = 1e20;
    test.input.maxX = 1e20;
    test.input.minY = 1e20;
    test.input.maxY = 1e20;

    const auto report =
        LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "sp" ),
                               QVector<AuditSample>{ train, test }, config );
    REQUIRE( report.has_value() );
    // The exclusion is QUANTIFIED: the report names how many samples the
    // spatial grid could not place, so "clean" cannot overstate coverage.
    CHECK( report->unbucketedCount() == 1 );
    CHECK( report->summary().value( QStringLiteral( "unbucketed_count" ) ).toInt() == 1 );

    // Round-trip: the count survives toJson/fromJson.
    const auto reloaded = LeakageReport::fromJson( report->toJson() );
    REQUIRE( reloaded.has_value() );
    CHECK( reloaded->unbucketedCount() == 1 );

    // Control: ordinary coordinates still land in the grid and a genuinely
    // close cross-split pair still produces a spatial finding — identical
    // centers make the distance check fire strictly below the 1.0 threshold
    // (exactly-at-threshold is intentionally not a finding), and no window
    // dims means the overlap check correctly stays out (its #1584 both-sides
    // evidence gate).
    const AuditSample trainNear = sampleWithBounds( QStringLiteral( "train-near" ),
                                                    SplitRole::Train, 0.0, 0.0, 10.0, 10.0 );
    const AuditSample testNear = sampleWithBounds( QStringLiteral( "test-near" ),
                                                   SplitRole::Test, 0.0, 0.0, 10.0, 10.0 );
    const auto control = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ),
        QVector<AuditSample>{ trainNear, testNear }, config );
    REQUIRE( control.has_value() );
    CHECK( control->unbucketedCount() == 0 );
    CHECK_FALSE( control->isClean() );
}

TEST_CASE( "a failed schema-gate read refuses open instead of stamping the "
           "store (R24-B)",
           "[r24_b][dataset][store][schema-gate]" )
{
  QTemporaryDir dir;
  const QString dbPath = dir.filePath( QStringLiteral( "gate.db" ) );

  // A store with a foreign NEWER schema stamp: the normal read path.
  {
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    // Hand-write a newer stamp directly through the store's own sqlite
    // handle is not exposed; use a fresh sqlite3 to forge the gate value.
  }
  // Forge schema_version = 99 via raw sqlite (the same trick the
  // forward-tolerance tests use).
  {
    sqlite3 *db = nullptr;
    REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &db ) == SQLITE_OK );
    char *err = nullptr;
    REQUIRE( sqlite3_exec( db,
                           "INSERT OR REPLACE INTO ds_meta(key,value)"
                           " VALUES('schema_version','99')",
                           nullptr, nullptr, &err ) == SQLITE_OK );
    if ( err )
      sqlite3_free( err );
    sqlite3_close( db );
  }

  // Baseline behavior (unchanged): newer stamp → read-only open.
  {
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    CHECK( store.isReadOnly() );
  }

  // Corrupt the page the gate SELECT actually traverses (R2 review: the
  // first attempt scribbled at size/2, which lands in a table the gate never
  // reads — the SELECT still succeeded and the test asserted against the
  // forward-tolerance branch instead). Page 2 is ds_meta's root page in a
  // fresh store (page 1 = sqlite_master root); smashing it makes
  // "SELECT value FROM ds_meta" fail with SQLITE_CORRUPT.
  {
    QFile file( dbPath );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.size() >= 3 * 4096 );
    REQUIRE( file.seek( 4096 ) );
    QByteArray garbage( 4096, '\x7f' );
    REQUIRE( file.write( garbage ) == garbage.size() );
    file.close();
  }
  DatasetStore corrupted;
  CHECK_FALSE( corrupted.open( dbPath ) );
}
