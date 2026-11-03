// tests/test_swarm_fix_vendored.cpp — swarm-review 2026-11-04 VD-series
// regression guards for the vendored QGIS tree (SWARM_REVIEW.md VD-1..VD-4).
//
// VD-1  the .qgz open path refused legitimate archives. QgsZipUtils::unzip is
//        the only entry point behind QgsArchive::unzip <- QgsProject::unzip,
//        and it hard-refused any single entry above 256 MiB, any archive above
//        512 MiB and any archive with more than 4096 entries — so a project
//        embedding a large attachment (raster/video/font pack) could not be
//        opened at all. The caps are now product limits (8 GiB per entry, 32
//        GiB per archive, 65536 entries); the fixtures below pin each raised
//        bound with an archive the old caps rejected and the new caps accept.
// VD-2  the hardening loop ignored zip_stat_index()'s return value. On failure
//        that call leaves the caller's struct untouched (it does not
//        zip_stat_init() it), so the size bound and the new char[len] consumed
//        stack garbage — and a null zip_fopen_index() handle was handed
//        straight to zip_fread(). Both now fail closed: a corrupt archive must
//        make unzip() return FALSE, never crash or read garbage.
// VD-3  unregisterExternalRenderJob() disconnected EVERY destroyed() receiver
//        of the job, not just the registry's own prune hook — any connection a
//        future registrant made to the job's destroyed() signal (with the
//        canvas as receiver) was silently eaten. It now disconnects precisely
//        the one connection it created.
// VD-4  settleExternalRenderJobs() relied on cancel() being blocking, which is
//        only documented and only verified for QgsMapRendererParallelJob. A
//        registrant handing the registry a job whose cancel() does not block
//        would get a "settled" canvas whose worker still holds raw
//        QgsMapLayer pointers — the #1389 race class. The settle now also waits
//        on the job (base-class contract) for every subclass.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsmaprendererjob.h>
#include <qgsmaprendererparalleljob.h>
#include <qgsmapsettings.h>
#include <qgsziputils.h>

#include "support/qt_lifecycle.h"

#include <cstdint>
#include <vector>

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
    int fake_argc = 1;
    char fake_argv0[] = "test_swarm_fix_vendored";
    char *fake_argv[] = { fake_argv0, nullptr };

    void ensureQgsApplication()
    {
        static bool initialized = false;
        if ( initialized )
            return;
        sicnu::test::qtlifecycle::heapQgsApplication( fake_argc, fake_argv, false );
        QgsApplication::initQgis();
        initialized = true;
    }

    // -----------------------------------------------------------------------
    // Minimal hand-rolled ZIP writer (stored entries).
    //
    // QgsZipUtils::zip() cannot express the archives VD-1 needs: a declared
    // uncompressed size far above the number of bytes actually stored. The
    // fixtures are therefore written byte by byte. An entry whose declared
    // size exceeds its stored bytes leaves a sparse hole in the file, which is
    // what keeps a 260 MiB fixture cheap — the payload is a hole that reads
    // back as zeroes, so the extraction cost is a memcpy of zeroes and nothing
    // is written to disk (directory-style names are skipped by unzip() after
    // the read, see below).
    // -----------------------------------------------------------------------

    struct ZipEntry
    {
        QString name;
        quint64 declaredSize = 0;             //!< uncompressed size in both headers
        QByteArray data;                      //!< stored bytes (a payload prefix)
        quint32 crc = 0;                      //!< CRC-32 of the entry's full payload
        bool bogusLocalHeaderOffset = false;  //!< VD-2: point the CD at a bogus local header
    };

    void put16( QFile &file, const quint16 value )
    {
        const char bytes[2] = { static_cast<char>( value & 0xFF ), static_cast<char>( ( value >> 8 ) & 0xFF ) };
        file.write( bytes, 2 );
    }

    void put32( QFile &file, const quint32 value )
    {
        const char bytes[4] = {
            static_cast<char>( value & 0xFF ),
            static_cast<char>( ( value >> 8 ) & 0xFF ),
            static_cast<char>( ( value >> 16 ) & 0xFF ),
            static_cast<char>( ( value >> 24 ) & 0xFF )
        };
        file.write( bytes, 4 );
    }

    void put64( QFile &file, const quint64 value )
    {
        const char bytes[8] = {
            static_cast<char>( value & 0xFF ),
            static_cast<char>( ( value >> 8 ) & 0xFF ),
            static_cast<char>( ( value >> 16 ) & 0xFF ),
            static_cast<char>( ( value >> 24 ) & 0xFF ),
            static_cast<char>( ( value >> 32 ) & 0xFF ),
            static_cast<char>( ( value >> 40 ) & 0xFF ),
            static_cast<char>( ( value >> 48 ) & 0xFF ),
            static_cast<char>( ( value >> 56 ) & 0xFF )
        };
        file.write( bytes, 8 );
    }

    quint32 crc32Of( const QByteArray &data )
    {
        static quint32 table[256];
        static bool ready = false;
        if ( !ready )
        {
            for ( quint32 i = 0; i < 256; ++i )
            {
                quint32 c = i;
                for ( int k = 0; k < 8; ++k )
                    c = ( c & 1u ) ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;
                table[i] = c;
            }
            ready = true;
        }
        quint32 crc = 0xFFFFFFFFu;
        for ( const char byte : data )
            crc = table[( crc ^ static_cast<quint8>( byte ) ) & 0xFFu] ^ ( crc >> 8 );
        return crc ^ 0xFFFFFFFFu;
    }

    //! CRC-32 of an all-zero payload of the given length. Precomputed because
    //! the sparse fixtures' payload is a hole that reads back as zeroes (and
    //! libzip does not verify the CRC while extracting, so the value is belt
    //! and braces for archive validity).
    constexpr quint32 kCrc32Of200MiBZeros = 0x534f0861u;
    constexpr quint32 kCrc32Of260MiBZeros = 0x5264d64cu;

    bool writeStoreZip( const QString &path, const std::vector<ZipEntry> &entries )
    {
        QFile file( path );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
            return false;

        std::vector<quint64> localHeaderOffsets;
        localHeaderOffsets.reserve( entries.size() );
        for ( const ZipEntry &entry : entries )
        {
            const QByteArray name = entry.name.toUtf8();
            const quint64 stored = static_cast<quint64>( entry.data.size() );
            localHeaderOffsets.push_back( static_cast<quint64>( file.pos() ) );

            // local file header (stored entry: compressed size == uncompressed size)
            put32( file, 0x04034b50 );
            put16( file, 20 );                 // version needed
            put16( file, 0 );                  // flags
            put16( file, 0 );                  // method: stored
            put16( file, 0 );                  // mod time
            put16( file, 0 );                  // mod date
            put32( file, entry.crc );
            put32( file, static_cast<quint32>( entry.declaredSize ) );
            put32( file, static_cast<quint32>( entry.declaredSize ) );
            put16( file, static_cast<quint16>( name.size() ) );
            put16( file, 0 );                  // extra length
            file.write( name );
            file.write( entry.data );
            if ( stored < entry.declaredSize )
            {
                // sparse hole standing in for the rest of the declared payload
                const quint64 next = static_cast<quint64>( file.pos() ) + ( entry.declaredSize - stored );
                file.resize( static_cast<qint64>( next ) );
                file.seek( static_cast<qint64>( next ) );
            }
        }

        const quint64 centralDirOffset = static_cast<quint64>( file.pos() );
        for ( size_t i = 0; i < entries.size(); ++i )
        {
            const QByteArray name = entries[i].name.toUtf8();
            const quint64 offset = entries[i].bogusLocalHeaderOffset ? 0x7ffffff0ull : localHeaderOffsets[i];
            put32( file, 0x02014b50 );
            put16( file, 20 );                 // version made by
            put16( file, 20 );                 // version needed
            put16( file, 0 );                  // flags
            put16( file, 0 );                  // method
            put16( file, 0 );                  // mod time
            put16( file, 0 );                  // mod date
            put32( file, entries[i].crc );
            put32( file, static_cast<quint32>( entries[i].declaredSize ) );
            put32( file, static_cast<quint32>( entries[i].declaredSize ) );
            put16( file, static_cast<quint16>( name.size() ) );
            put16( file, 0 );                  // extra length
            put16( file, 0 );                  // comment length
            put16( file, 0 );                  // disk number start
            put16( file, 0 );                  // internal attributes
            put32( file, 0 );                  // external attributes
            put32( file, static_cast<quint32>( offset ) );
            file.write( name );
        }
        const quint64 centralDirSize = static_cast<quint64>( file.pos() ) - centralDirOffset;

        // end of central directory; a zip64 EOCD is required once the entry
        // count no longer fits the 16-bit field.
        const bool needsZip64 = entries.size() > 0xFFFFu
                                || centralDirOffset > 0xFFFFFFFFull
                                || centralDirSize > 0xFFFFFFFFull;
        if ( needsZip64 )
        {
            const quint64 zip64Offset = static_cast<quint64>( file.pos() );
            put32( file, 0x06064b50 );         // zip64 end of central directory
            put64( file, 44 );                 // size of the remaining record
            put16( file, 45 );                 // version made by
            put16( file, 45 );                 // version needed
            put32( file, 0 );                  // number of this disk
            put32( file, 0 );                  // disk with the central directory
            put64( file, entries.size() );     // entries on this disk
            put64( file, entries.size() );     // total entries
            put64( file, centralDirSize );
            put64( file, centralDirOffset );
            put32( file, 0x07064b50 );         // zip64 EOCD locator
            put32( file, 0 );                  // disk with the zip64 EOCD
            put64( file, zip64Offset );
            put32( file, 1 );                  // total number of disks
        }
        put32( file, 0x06054b50 );
        put16( file, 0 );                      // number of this disk
        put16( file, 0 );                      // disk with the central directory
        put16( file, needsZip64 ? 0xFFFF : static_cast<quint16>( entries.size() ) );
        put16( file, needsZip64 ? 0xFFFF : static_cast<quint16>( entries.size() ) );
        put32( file, needsZip64 ? 0xFFFFFFFFu : static_cast<quint32>( centralDirSize ) );
        put32( file, needsZip64 ? 0xFFFFFFFFu : static_cast<quint32>( centralDirOffset ) );
        put16( file, 0 );                      // comment length

        return file.flush() && file.error() == QFile::NoError;
    }

    //! A valid three-entry archive used as the corruption base for VD-2.
    std::vector<ZipEntry> threeSmallEntries()
    {
        std::vector<ZipEntry> entries;
        const QStringList names = { u"a.txt"_s, u"dir/b.txt"_s, u"c.txt"_s };
        const QStringList payloads = { u"hello"_s, u"world"_s, u"!"_s };
        for ( int i = 0; i < names.size(); ++i )
        {
            ZipEntry entry;
            entry.name = names[i];
            entry.data = payloads[i].toUtf8();
            entry.declaredSize = static_cast<quint64>( entry.data.size() );
            entry.crc = crc32Of( entry.data );
            entries.push_back( entry );
        }
        return entries;
    }

    //! A job that records what the canvas registry asks of it. VD-4 needs a
    //! subclass whose cancel() is NOT blocking: that is the shape the old
    //! settle silently mishandled, and a recording job turns "did the settle
    //! wait?" into a deterministic assertion instead of a threaded race.
    class RecordingJob : public QgsMapRendererJob
    {
      public:
        explicit RecordingJob( const QgsMapSettings &settings )
            : QgsMapRendererJob( settings )
        {}
        ~RecordingJob() override = default;

        int cancelCalls = 0;
        int waitCalls = 0;

        void cancel() override { ++cancelCalls; }
        void cancelWithoutBlocking() override {}
        void waitForFinished() override { ++waitCalls; }
        bool isActive() const override { return false; }
        bool usedCachedLabels() const override { return false; }
        QgsLabelingResults *takeLabelingResults() override { return nullptr; }

      protected:
        void startPrivate() override {}
    };
} // namespace

// ---------------------------------------------------------------------------
// VD-1: raised absolute caps
// ---------------------------------------------------------------------------

TEST_CASE( "swarm-review VD-1: a single .qgz entry larger than the old 256 MiB cap is accepted",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"big-entry.qgz"_s );

    // 260 MiB > the old 256 MiB per-entry cap, < the old 512 MiB total cap:
    // this isolates the per-entry bound. The trailing slash makes unzip()
    // skip the payload write (directory entries are skipped after the read),
    // so the fixture costs a sparse hole and no extracted bytes.
    ZipEntry entry;
    entry.name = u"big.bin/"_s;
    entry.declaredSize = 260ull * 1024ull * 1024ull;
    entry.crc = kCrc32Of260MiBZeros;
    REQUIRE( writeStoreZip( zipPath, { entry } ) );

    QStringList files;
    CHECK( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.isEmpty() );
}

TEST_CASE( "swarm-review VD-1: an archive whose total exceeds the old 512 MiB cap is accepted",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"big-total.qgz"_s );

    // Three 200 MiB entries: each below the old 256 MiB per-entry cap, 600 MiB
    // together above the old 512 MiB total cap — this isolates the total bound.
    std::vector<ZipEntry> entries;
    for ( const QString &name : { u"a/"_s, u"b/"_s, u"c/"_s } )
    {
        ZipEntry entry;
        entry.name = name;
        entry.declaredSize = 200ull * 1024ull * 1024ull;
        entry.crc = kCrc32Of200MiBZeros;
        entries.push_back( entry );
    }
    REQUIRE( writeStoreZip( zipPath, entries ) );

    QStringList files;
    CHECK( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.isEmpty() );
}

TEST_CASE( "swarm-review VD-1: an archive with more than the old 4096 entries is extracted",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"many-entries.qgz"_s );

    // 5000 real entries: above the old 4096 entry cap, below the new 65536.
    // This one really extracts, so it also pins the extraction path itself.
    std::vector<ZipEntry> entries;
    for ( int i = 0; i < 5000; ++i )
    {
        ZipEntry entry;
        entry.name = QStringLiteral( "attach/%1.txt" ).arg( i, 5, 10, QLatin1Char( '0' ) );
        entry.data = QByteArrayLiteral( "payload" );
        entry.declaredSize = static_cast<quint64>( entry.data.size() );
        entry.crc = crc32Of( entry.data );
        entries.push_back( entry );
    }
    REQUIRE( writeStoreZip( zipPath, entries ) );

    QStringList files;
    REQUIRE( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.size() == 5000 );
    const QString probe = QDir( outDir.path() ).absoluteFilePath( u"attach/00042.txt"_s );
    CHECK( files.contains( probe ) );
    QFile extracted( probe );
    REQUIRE( extracted.open( QIODevice::ReadOnly ) );
    CHECK( extracted.readAll() == QByteArrayLiteral( "payload" ) );
}

TEST_CASE( "swarm-review VD-1: the entry-count bound is still enforced above 65536",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"too-many-entries.qgz"_s );

    // The caps were raised, not removed: an archive declaring more than 65536
    // entries must still be refused (and refused before anything is written).
    std::vector<ZipEntry> entries;
    for ( int i = 0; i < 65537; ++i )
    {
        ZipEntry entry;
        entry.name = QStringLiteral( "a%1" ).arg( i );
        entry.data = QByteArrayLiteral( "x" );
        entry.declaredSize = 1;
        entry.crc = crc32Of( entry.data );
        entries.push_back( entry );
    }
    REQUIRE( writeStoreZip( zipPath, entries ) );

    QStringList files;
    CHECK_FALSE( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.isEmpty() );
}

// ---------------------------------------------------------------------------
// VD-2: fail closed on corrupt archives
// ---------------------------------------------------------------------------

TEST_CASE( "swarm-review VD-2: a corrupt central directory makes unzip fail closed",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );

    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"corrupt-cd.qgz"_s );
    REQUIRE( writeStoreZip( zipPath, threeSmallEntries() ) );

    // Overwrite the central directory with zeroes, keeping the EOCD: the
    // classic "damaged archive" a user (or an attacker) can produce. unzip()
    // must reject it outright — FALSE, nothing written, no crash — which is the
    // contract the hardening loop around zip_stat_index()/zip_fopen_index()
    // exists to guarantee.
    QFile zip( zipPath );
    REQUIRE( zip.open( QIODevice::ReadWrite ) );
    const QByteArray eocd = zip.readAll();
    const int eocdOffset = eocd.lastIndexOf( QByteArrayLiteral( "PK\x05\x06" ) );
    REQUIRE( eocdOffset >= 0 );
    const quint32 centralDirOffset = static_cast<quint32>( static_cast<quint8>( eocd.at( eocdOffset + 16 ) ) )
                                     | ( static_cast<quint32>( static_cast<quint8>( eocd.at( eocdOffset + 17 ) ) ) << 8 )
                                     | ( static_cast<quint32>( static_cast<quint8>( eocd.at( eocdOffset + 18 ) ) ) << 16 )
                                     | ( static_cast<quint32>( static_cast<quint8>( eocd.at( eocdOffset + 19 ) ) ) << 24 );
    REQUIRE( centralDirOffset > 0 );
    zip.seek( static_cast<qint64>( centralDirOffset ) );
    CHECK( zip.write( QByteArray( static_cast<int>( eocdOffset - static_cast<int>( centralDirOffset ) ), '\0' ) )
           == static_cast<qint64>( eocdOffset - static_cast<int>( centralDirOffset ) ) );
    zip.close();

    QStringList files;
    CHECK_FALSE( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.isEmpty() );
}

TEST_CASE( "swarm-review VD-2: an unopenable entry makes unzip fail closed",
           "[swarm][vendored][zip]" )
{
    ensureQgsApplication();
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    const QString zipPath = QDir( outDir.path() ).absoluteFilePath( u"bad-entry-offset.qgz"_s );

    // Control: the same archive, uncorrupted, extracts.
    const std::vector<ZipEntry> clean = threeSmallEntries();
    REQUIRE( writeStoreZip( zipPath, clean ) );
    QStringList files;
    REQUIRE( QgsZipUtils::unzip( zipPath, outDir.path(), files ) );
    CHECK( files.size() == 3 );

    // Now point the central directory's local-header offset at a bogus file
    // offset. zip_open() without the consistency check still accepts the
    // archive (the central directory itself is intact), so the loop reaches
    // zip_fopen_index() — which fails. The new guard fails closed instead of
    // handing the null handle to zip_fread(): that is undefined behaviour per
    // libzip's API contract (the pre-fix code only got away with it because
    // this libzip build happens to tolerate a null zip_file).
    // NOTE: no on-disk corruption can reach the zip_stat_index() guard — libzip
    // rejects an EOCD entry count that disagrees with the parsed central
    // directory — so that half of VD-2 is defence in depth, and this test pins
    // the fail-closed contract of the loop it protects.
    std::vector<ZipEntry> corruptedFirst = clean;
    corruptedFirst[0].bogusLocalHeaderOffset = true;
    REQUIRE( writeStoreZip( zipPath, corruptedFirst ) );

    QStringList filesFirstEntryBad;
    CHECK_FALSE( QgsZipUtils::unzip( zipPath, outDir.path(), filesFirstEntryBad, false ) );
    CHECK( filesFirstEntryBad.isEmpty() );

    // The same corruption on a middle entry: the entries before it are already
    // on disk (unzip() is not atomic), the walk must stop there and return
    // FALSE rather than crash.
    std::vector<ZipEntry> corruptedMiddle = clean;
    corruptedMiddle[1].bogusLocalHeaderOffset = true;
    REQUIRE( writeStoreZip( zipPath, corruptedMiddle ) );

    QStringList filesMiddleEntryBad;
    CHECK_FALSE( QgsZipUtils::unzip( zipPath, outDir.path(), filesMiddleEntryBad, false ) );
    CHECK( filesMiddleEntryBad.size() == 1 );

    // With the default consistency check the same archive is rejected at open
    // time; either way unzip() must return FALSE and leave nothing behind.
    QTemporaryDir outDir2;
    REQUIRE( outDir2.isValid() );
    QStringList filesWithConsistency;
    CHECK_FALSE( QgsZipUtils::unzip( zipPath, outDir2.path(), filesWithConsistency, true ) );
    CHECK( filesWithConsistency.isEmpty() );
}

// ---------------------------------------------------------------------------
// VD-3: precise disconnect
// ---------------------------------------------------------------------------

TEST_CASE( "swarm-review VD-3: unregisterExternalRenderJob keeps other destroyed() connections",
           "[swarm][vendored][mapcanvas]" )
{
    ensureQgsApplication();
    QgsMapCanvas canvas;

    auto *job = new QgsMapRendererParallelJob( QgsMapSettings() );
    canvas.registerExternalRenderJob( job );

    // An application-side connection to the job's destroyed() signal with the
    // canvas as receiver — exactly what a future registrant would add. The old
    // disconnect(job, &QObject::destroyed, this, nullptr) ate this one.
    bool applicationSideFired = false;
    QObject::connect( job, &QObject::destroyed, &canvas, [&applicationSideFired]() {
        applicationSideFired = true;
    } );

    canvas.unregisterExternalRenderJob( job );
    delete job;

    CHECK( applicationSideFired );
}

TEST_CASE( "swarm-review VD-3: a destroyed registered job is pruned from the registry",
           "[swarm][vendored][mapcanvas]" )
{
    ensureQgsApplication();
    QgsMapCanvas canvas;

    // The registry's own destroyed() hook must still prune the job (and its
    // connection) when the owner never unregisters: settling afterwards walks
    // the registry, so a stale pointer here would be a use-after-free.
    auto *job = new RecordingJob( QgsMapSettings {} );
    canvas.registerExternalRenderJob( job );
    delete job;

    canvas.stopRenderingAndSettle();
    CHECK_FALSE( canvas.isDrawing() );
}

// ---------------------------------------------------------------------------
// VD-4: settle waits, it does not just cancel
// ---------------------------------------------------------------------------

TEST_CASE( "swarm-review VD-4: settleExternalRenderJobs cancels and waits on every job",
           "[swarm][vendored][mapcanvas]" )
{
    ensureQgsApplication();
    QgsMapCanvas canvas;

    const QgsMapSettings settings;
    RecordingJob job( settings );
    canvas.registerExternalRenderJob( &job );

    canvas.stopRenderingAndSettle();

    // The settle must cancel AND wait. A job whose cancel() is not blocking
    // (every subclass except QgsMapRendererParallelJob in the old code path)
    // would otherwise be reported as settled while its worker still holds raw
    // layer pointers — the #1389 race class.
    CHECK( job.cancelCalls == 1 );
    CHECK( job.waitCalls == 1 );
}
