// test_model_publish_fence_r5.cpp — R5 Track 07: publish-fence path identity
// and detection-guard occupancy (src/operators/runtime/model_publish).
//
// The #1353 residual this suite closes: the R4 fence keyed on
// QDir::cleanPath(finalPath) — a lexical normalization only. Two spellings of
// ONE artifact ("./out.tif" vs "<cwd>/out.tif" vs a symlinked-directory
// spelling) got INDEPENDENT fences, so concurrent publishes of the same
// artifact could adopt or delete each other's parked products. And
// DetectionPublishGuard — same adoption/park machinery, same corruption
// window — had NO fence at all.
//
// Contracts pinned here:
//   1. one artifact, many spellings -> ONE fence (AlreadyRunning for the
//      second guard), across lexical relatives, "."/".." traversal and
//      symlinked directories;
//   2. distinct artifacts keep independent fences;
//   3. a throwing constructor releases the canonical slot: after a real park
//      failure the retry is refused with the PARK error, never AlreadyRunning
//      (a leaked slot would surface as AlreadyRunning — the #1353 P0 class);
//   4. DetectionPublishGuard refuses a second concurrent guard typed;
//   5. concurrent guard construction has exactly one winner per round;
//   6. case handling is probed, never assumed: on a case-sensitive filesystem
//      two case-distinct names keep distinct fences; the expectation is
//      derived from stat ground truth, so the oracle holds on BOTH flavors.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_publish.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <barrier>
#include <chrono>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <thread>

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

namespace
{
constexpr const char *kSuffix = ".prev~";

/// Restores the process cwd on scope exit (relative-spelling oracles).
struct ScopedCwd
{
    explicit ScopedCwd( const QString &newDir )
        : m_old( QDir::currentPath() )
    {
        REQUIRE( QDir::setCurrent( newDir ) );
    }
    ~ScopedCwd() { QDir::setCurrent( m_old ); }
    ScopedCwd( const ScopedCwd & ) = delete;
    ScopedCwd &operator=( const ScopedCwd & ) = delete;

  private:
    QString m_old;
};

bool isCaseInsensitiveLocation( const QString &existingPath )
{
    struct stat probe{};
    if ( ::stat( QFile::encodeName( existingPath ).constData(), &probe ) != 0 )
        return false;
    // Case-flip the first letter of the last component.
    const int start = existingPath.lastIndexOf( QLatin1Char( '/' ) ) + 1;
    for ( int i = start; i < existingPath.size(); ++i )
    {
        const QChar c = existingPath.at( i );
        if ( c.isLetter() )
        {
            QString flipped = existingPath;
            flipped[i] = c.isUpper() ? c.toLower() : c.toUpper();
            struct stat alias{};
            if ( ::stat( QFile::encodeName( flipped ).constData(), &alias ) != 0 )
                return false;
            return probe.st_dev == alias.st_dev && probe.st_ino == alias.st_ino;
        }
    }
    return false;
}

std::string tryConstruct( const std::function<void()> &ctor )
{
    try
    {
        ctor();
        return {};
    }
    catch ( const RSOperatorError &e )
    {
        return errorCodeToString( e.code() );
    }
}
} // namespace

TEST_CASE( "One artifact under many spellings holds ONE publish fence",
           "[runtime][publish][fence][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString abs = dir.filePath( QStringLiteral( "product.tif" ) ); // does not exist yet
    const QString dirName = QDir( dir.path() ).dirName();

    // The first guard holds its fence for its WHOLE lifetime (disarm() only
    // marks the product as settled — the occupancy releases on destruction,
    // after the restore machinery is gone), so it lives in an inner scope.
    {
        ProductPublishGuard first( abs, QLatin1String( kSuffix ), nullptr );

        // Lexical noise on an absolute spelling.
        const QString dotted = dir.filePath( QStringLiteral( "./sub/../product.tif" ) );
        CHECK( tryConstruct( [ & ] { ProductPublishGuard g( dotted, QLatin1String( kSuffix ), nullptr ); } )
               == errorCodeToString( ErrorCode::AlreadyRunning ) );

        // Relative spellings against the SAME cwd.
        {
            ScopedCwd cwd( dir.path() );
            CHECK( tryConstruct( [ & ] { ProductPublishGuard g( QStringLiteral( "product.tif" ), QLatin1String( kSuffix ), nullptr ); } )
                   == errorCodeToString( ErrorCode::AlreadyRunning ) );
            CHECK( tryConstruct( [ & ] {
                      ProductPublishGuard g( QStringLiteral( "../" ) + dirName + QStringLiteral( "/product.tif" ),
                                             QLatin1String( kSuffix ), nullptr );
                  } )
                   == errorCodeToString( ErrorCode::AlreadyRunning ) );
        }

        // Symlinked DIRECTORY spelling: <dir>/product.tif through a link.
        const QString dirLink = dir.filePath( QStringLiteral( "dirlink" ) );
        REQUIRE( QFile::link( dir.path(), dirLink ) );
        CHECK( tryConstruct( [ & ] { ProductPublishGuard g( dirLink + QStringLiteral( "/product.tif" ), QLatin1String( kSuffix ), nullptr ); } )
               == errorCodeToString( ErrorCode::AlreadyRunning ) );
    }

    // A genuinely different artifact is NOT blocked.
    {
        ProductPublishGuard other( dir.filePath( QStringLiteral( "other.tif" ) ), QLatin1String( kSuffix ), nullptr );
        other.disarm();
    }

    // After the first guard is DESTROYED, every spelling is publishable again.
    {
        ScopedCwd cwd( dir.path() );
        ProductPublishGuard republish( QStringLiteral( "product.tif" ), QLatin1String( kSuffix ), nullptr );
        republish.disarm();
    }
}

TEST_CASE( "A throwing constructor releases the canonical slot for ALL spellings",
           "[runtime][publish][fence][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A real park failure, no fault machinery: the previous product exists
    // and its backup path is a DIRECTORY, so the park rename cannot succeed.
    QFile product( dir.filePath( QStringLiteral( "product.tif" ) ) );
    REQUIRE( product.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    product.write( "previous-product" );
    product.close();
    REQUIRE( QDir( dir.path() ).mkpath( QStringLiteral( "product.tif" ) + QLatin1String( kSuffix ) ) );

    // Throw through the relative spelling…
    const std::string firstError = tryConstruct( [ & ] {
        ScopedCwd cwd( dir.path() );
        ProductPublishGuard g( QStringLiteral( "./product.tif" ), QLatin1String( kSuffix ), nullptr );
    } );
    CHECK( firstError == errorCodeToString( ErrorCode::FileNotWritable ) );

    // …the retry through the absolute spelling must fail with the PARK error
    // again — an AlreadyRunning here would mean the throwing constructor
    // leaked its canonical fence slot (the #1353 P0 class).
    const std::string secondError = tryConstruct( [ & ] {
        ProductPublishGuard g( dir.filePath( QStringLiteral( "product.tif" ) ), QLatin1String( kSuffix ), nullptr );
    } );
    CHECK( secondError == errorCodeToString( ErrorCode::FileNotWritable ) );

    // Remove the blocking backup directory: the publish now goes through.
    REQUIRE( QDir( dir.path() ).rmdir( QStringLiteral( "product.tif" ) + QLatin1String( kSuffix ) ) );
    ProductPublishGuard recovered( dir.filePath( QStringLiteral( "product.tif" ) ), QLatin1String( kSuffix ), nullptr );
    recovered.disarm();
}

TEST_CASE( "DetectionPublishGuard refuses a second concurrent guard on the same artifact",
           "[runtime][publish][fence][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString shapefile = dir.filePath( QStringLiteral( "detections.shp" ) );

    {
        DetectionPublishGuard first( shapefile, QStringLiteral( ".det-prev~" ) );
        CHECK( tryConstruct( [ & ] { DetectionPublishGuard g( shapefile, QStringLiteral( ".det-prev~" ) ); } )
               == errorCodeToString( ErrorCode::AlreadyRunning ) );
        // Alias spelling of the same artifact is refused too.
        CHECK( tryConstruct( [ & ] {
                  DetectionPublishGuard g( dir.filePath( QStringLiteral( "./detections.shp" ) ), QStringLiteral( ".det-prev~" ) );
              } )
               == errorCodeToString( ErrorCode::AlreadyRunning ) );
        // A different artifact publishes concurrently.
        {
            DetectionPublishGuard other( dir.filePath( QStringLiteral( "other.shp" ) ), QStringLiteral( ".det-prev~" ) );
            other.disarm();
        }
    }
    // After the first guard is destroyed, the artifact is publishable again.
    DetectionPublishGuard after( shapefile, QStringLiteral( ".det-prev~" ) );
    after.disarm();
}

TEST_CASE( "Concurrent guard construction on one artifact has exactly one winner per round",
           "[runtime][publish][fence][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString abs = dir.filePath( QStringLiteral( "raced.tif" ) );

    std::atomic<int> winners{ 0 };
    std::atomic<int> refused{ 0 };
    for ( int round = 0; round < 4; ++round )
    {
        winners.store( 0 );
        refused.store( 0 );
        std::barrier start( 2 );
        auto contender = [ &, round ]( int id ) {
            start.arrive_and_wait();
            const QString spelling = id == 0 ? abs
                                             : dir.filePath( QStringLiteral( "./raced.tif" ) );
            try
            {
                ProductPublishGuard g( spelling, QLatin1String( kSuffix ), nullptr );
                // Hold the fence long enough that a loser descheduled for a
                // few ticks still finds the slot taken.
                std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
                g.disarm();
                winners.fetch_add( 1 );
            }
            catch ( const RSOperatorError & )
            {
                refused.fetch_add( 1 );
            }
        };
        std::thread a( contender, 0 );
        std::thread b( contender, 1 );
        a.join();
        b.join();
        INFO( "round " << round << ": winners=" << winners.load() << " refused=" << refused.load() );
        CHECK( winners.load() == 1 );
        CHECK( refused.load() == 1 );
    }
}

TEST_CASE( "Fence identity follows the probed filesystem case rules",
           "[runtime][publish][fence][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString anchor = dir.path(); // an EXISTING directory: probe target

    // Ground truth from the filesystem itself — this oracle holds on both
    // case-sensitive and case-insensitive filesystems without skipping.
    const bool caseInsensitive = isCaseInsensitiveLocation( anchor );

    const QString upper = dir.filePath( QStringLiteral( "ARTIFACT.tif" ) );
    const QString lower = dir.filePath( QStringLiteral( "artifact.tif" ) );

    const std::string keyUpper = canonicalPublishFenceKey( upper );
    const std::string keyLower = canonicalPublishFenceKey( lower );
    if ( caseInsensitive )
        CHECK( keyUpper == keyLower ); // one file, one fence
    else
        CHECK( keyUpper != keyLower ); // two genuinely distinct files

    // An existing file reached through a SYMLINK folds onto the target's key.
    QFile file( lower );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( "payload" );
    file.close();
    const QString link = dir.filePath( QStringLiteral( "alias.tif" ) );
    REQUIRE( QFile::link( lower, link ) );
    CHECK( canonicalPublishFenceKey( link ) == canonicalPublishFenceKey( lower ) );
}
