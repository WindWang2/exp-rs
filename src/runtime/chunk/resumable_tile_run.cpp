// resumable_tile_run.cpp — see resumable_tile_run.h for the resume laws R1-R6.
#include "resumable_tile_run.h"

#include "runtime/observability/execution_telemetry.h"
#include "runtime/observability/fault_point.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace sicnu::runtime::chunk
{

namespace
{

constexpr char kJournalHeaderVersion = '1';

/// Parses "C <index> tile-<index>.tl" strictly: no extra tokens.
bool parseCommitLine( const std::string &line, std::uint64_t totalTiles,
                      std::uint64_t &indexOut )
{
    std::istringstream ls( line );
    std::string tag, name, extra;
    std::uint64_t index = 0;
    if ( !( ls >> tag >> index >> name ) )
        return false;
    ls >> extra; // must extract nothing
    if ( !extra.empty() )
        return false;
    if ( tag != "C" || index >= totalTiles )
        return false;
    if ( name != "tile-" + std::to_string( index ) + ".tl" )
        return false;
    indexOut = index;
    return true;
}

} // namespace

ResumableTileRun::ResumableTileRun( TileRunSpec spec, Config config )
    : m_spec( std::move( spec ) ), m_config( std::move( config ) )
{
    if ( m_config.scratchRoot.empty() || m_config.statePath.empty() )
        throw std::logic_error( "resumable tile run: scratchRoot and statePath are required" );
    m_totalTiles = m_spec.partition.totalTiles();
    if ( m_totalTiles == 0 )
        throw std::logic_error( "resumable tile run: partition has zero tiles" );

    m_identityKey = tileRunIdentityKey( m_spec.identity );
    m_runKey = "rt-" + m_identityKey;
    m_runDir = ( std::filesystem::path( m_config.scratchRoot ) / m_runKey ).generic_string();
    m_journalPath = m_config.statePath + ".journal";
    m_checkpointPath = m_config.statePath + ".ckpt";
    m_markerPath = m_config.statePath + ".published";
}

std::filesystem::path ResumableTileRun::tilePath( std::uint64_t index ) const
{
    return std::filesystem::path( m_runDir ) / ( "tile-" + std::to_string( index ) + ".tl" );
}

ResumableTileRun::JournalState ResumableTileRun::loadJournal() const
{
    JournalState state;
    state.committed.assign( static_cast<size_t>( m_totalTiles ), false );

    std::error_code ec;
    if ( !std::filesystem::exists( m_journalPath, ec ) || ec )
        return state; // fresh

    std::ifstream in( m_journalPath, std::ios::binary );
    if ( !in )
        throw ChunkCorruptTile( m_journalPath );

    std::string line;
    std::uint64_t lineStart = 0;
    bool headerSeen = false;
    while ( std::getline( in, line ) )
    {
        if ( !headerSeen )
        {
            // Expected: "V 1 <identityKey>".
            if ( line.rfind( "V ", 0 ) == 0 && line.size() > 4 && line[3] == ' '
                 && line[2] == kJournalHeaderVersion )
            {
                const std::string key = line.substr( 4 );
                if ( key != m_identityKey )
                {
                    // R1: a well-formed journal for ANOTHER identity is
                    // stale state from a drifted run — wipe and start clean,
                    // never mix identities, never trust foreign tiles.
                    wipeState( /*includeMarker=*/true );
                    return state;
                }
                headerSeen = true;
                lineStart += line.size() + 1;
                continue;
            }
            // Malformed header: a torn creation write (first, unterminated
            // line) truncates to empty; anything else is corruption (R3).
            std::string probe;
            const bool hasMore = static_cast<bool>( std::getline( in, probe ) );
            if ( !hasMore )
            {
                std::error_code truncEc;
                std::filesystem::resize_file( m_journalPath, 0, truncEc );
                if ( truncEc ) // failed truncation must not be appended after
                    throw std::runtime_error( "resumable tile run: journal torn-header "
                                              "truncation failed ("
                                              + truncEc.message() + ")" );
                return state;
            }
            throw ChunkCorruptTile( m_journalPath );
        }
        std::uint64_t index = 0;
        if ( parseCommitLine( line, m_totalTiles, index ) )
        {
            // Fold into the bitmap while scanning — O(1) memory beyond the
            // bitmap itself, duplicates collapse naturally.
            if ( !state.committed[static_cast<size_t>( index )] )
            {
                state.committed[static_cast<size_t>( index )] = true;
                ++state.committedCount;
            }
            lineStart += line.size() + 1;
            continue;
        }
        // Malformed commit line: torn tail (crash mid-append, nothing after
        // it) truncates to the last complete line; content after it means
        // mid-file corruption — fail closed (R3).
        std::string probe;
        if ( !std::getline( in, probe ) )
        {
            std::error_code truncEc;
            std::filesystem::resize_file( m_journalPath, lineStart, truncEc );
            if ( truncEc )
                throw std::runtime_error( "resumable tile run: journal torn-tail truncation "
                                          "failed (" + truncEc.message() + ")" );
            break;
        }
        throw ChunkCorruptTile( m_journalPath );
    }

    state.fresh = false;
    return state;
}

void ResumableTileRun::appendCommit( std::uint64_t index )
{
    if ( SICNU_FAULT_POINT( "exec11.journalAppend" ) )
        throw std::runtime_error( "resumable tile run: injected journal append failure" );

    // Header when the journal is missing OR EMPTY: a crash between file
    // creation and the first flush (and the torn-header recovery resize to
    // zero) both leave a 0-byte file that must NOT be continued headerless —
    // a headerless journal would be indistinguishable from corruption
    // (review P1 fix).
    std::error_code ec;
    bool needsHeader = true;
    if ( std::filesystem::exists( m_journalPath, ec ) && !ec )
    {
        const std::uintmax_t size = std::filesystem::file_size( m_journalPath, ec );
        needsHeader = ec || size == 0;
    }
    std::ofstream out( m_journalPath,
                       std::ios::binary | std::ios::app | std::ios::ate );
    if ( !out )
        throw std::runtime_error( "resumable tile run: cannot open journal " + m_journalPath );
    if ( needsHeader )
        out << "V " << kJournalHeaderVersion << ' ' << m_identityKey << '\n';
    out << "C " << index << ' ' << ( "tile-" + std::to_string( index ) + ".tl" ) << '\n';
    out.flush();
    if ( !out )
        throw std::runtime_error( "resumable tile run: journal append failed on "
                                  + m_journalPath );
}

void ResumableTileRun::saveCheckpoint( std::uint64_t committedCount ) const
{
    TileCheckpoint cp;
    cp.formatVersion = kTileCheckpointFormatVersion;
    cp.operatorIdentity = m_spec.identity.operatorIdentity;
    cp.inputIdentity = m_spec.identity.inputIdentity;
    cp.completedTiles = committedCount;
    cp.scratchRunId = m_runKey;
    if ( !TileCheckpointWriter::save( m_checkpointPath, cp ) )
        throw std::runtime_error( "resumable tile run: checkpoint save failed on "
                                  + m_checkpointPath );
}

bool ResumableTileRun::markerMatches() const
{
    std::error_code ec;
    if ( !std::filesystem::exists( m_markerPath, ec ) || ec )
        return false;
    std::ifstream in( m_markerPath, std::ios::binary );
    if ( !in )
        return false;
    std::string version, key;
    in >> version >> key;
    return version == std::string( 1, kJournalHeaderVersion ) && key == m_identityKey;
}

void ResumableTileRun::writeMarker() const
{
    if ( SICNU_FAULT_POINT( "exec11.marker" ) )
        throw std::runtime_error( "resumable tile run: injected marker failure" );
    const std::string tmp = m_markerPath + ".tmp";
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        if ( !out )
            throw std::runtime_error( "resumable tile run: cannot open marker tmp " + tmp );
        out << kJournalHeaderVersion << ' ' << m_identityKey << '\n';
        out.flush();
        if ( !out )
        {
            out.close();
            std::error_code removeEc;
            std::filesystem::remove( tmp, removeEc );
            throw std::runtime_error( "resumable tile run: marker tmp short write" );
        }
    }
    std::error_code renameEc;
    std::filesystem::rename( tmp, m_markerPath, renameEc );
    if ( renameEc )
    {
        std::error_code removeEc;
        std::filesystem::remove( tmp, removeEc );
        throw std::runtime_error( "resumable tile run: marker rename failed ("
                                  + renameEc.message() + ")" );
    }
}

void ResumableTileRun::wipeState( bool includeMarker ) const
{
    std::error_code ec;
    std::filesystem::remove_all( m_runDir, ec );
    std::filesystem::remove( m_journalPath, ec );
    std::filesystem::remove( m_checkpointPath, ec );
    if ( includeMarker )
        std::filesystem::remove( m_markerPath, ec );
}

ResumableTileRun::Result ResumableTileRun::execute( const TileRunCancelSource &cancel,
                                                    Callbacks cb )
{
    if ( !cb.compute || !cb.consume || !cb.publish )
        throw std::logic_error( "resumable tile run: compute/consume/publish callbacks required" );

    Result result;
    result.totalTiles = m_totalTiles;

    // R5: published marker proves this exact run identity completed.
    if ( markerMatches() )
    {
        result.alreadyPublished = true;
        return result;
    }
    // A marker for a DIFFERENT identity means the state paths belong to a
    // stale run: wipe and start clean (never mix identities).
    {
        std::error_code ec;
        if ( std::filesystem::exists( m_markerPath, ec ) && !ec )
            wipeState( true );
    }

    JournalState journal = loadJournal();

    // R1 (journal side): a journal for another identity is stale state.
    // loadJournal either matched the header or reset/failed; a mismatched
    // header surfaces as ChunkCorruptTile above. Checkpoint gates:
    auto ckpt = TileCheckpointWriter::load( m_checkpointPath,
                                             m_spec.identity.operatorIdentity,
                                             m_spec.identity.inputIdentity );
    if ( ckpt && ckpt->scratchRunId != m_runKey )
    {
        // Checkpoint belongs to a different run layout: stale, wipe (R1).
        wipeState( false );
        journal = loadJournal();
        ckpt = std::nullopt;
    }
    // NOTE: the checkpoint is ADVISORY position bookkeeping here (visible to
    // workflow/tooling); the journal is the sole resume source of truth.

    std::error_code dirEc;
    std::filesystem::create_directories( m_runDir, dirEc );
    if ( dirEc )
        throw std::runtime_error( "resumable tile run: cannot create run dir " + m_runDir
                                  + " (" + dirEc.message() + ")" );
    // The state sidecars may live anywhere the caller chose; create their
    // parent directory too (a journal that cannot be opened would fail at
    // the first commit — after tiles were already computed).
    std::error_code stateEc;
    const auto stateParent = std::filesystem::path( m_config.statePath ).parent_path();
    if ( !stateParent.empty() )
        std::filesystem::create_directories( stateParent, stateEc );
    if ( stateEc )
        throw std::runtime_error( "resumable tile run: cannot create state dir "
                                  + stateParent.generic_string() + " (" + stateEc.message() + ")" );

    // Resume visibility (WP-F): one RunResumed event when a restart attached
    // prior committed state; per-tile attach/compute hits the cache-hit /
    // cache-miss counters (exact, atomic — no per-tile events).
    auto &telemetry = observability::ExecutionTelemetry::instance();
    if ( !journal.fresh && journal.committedCount > 0 )
        telemetry.recordSimple( observability::EventKind::RunResumed, -1,
                                static_cast<std::int64_t>( journal.committedCount ),
                                "resumable_tile_run" );

    std::uint64_t sinceCheckpoint = 0;
    for ( std::uint64_t index = 0; index < m_totalTiles; ++index )
    {
        if ( cancel.cancelled() )
            throw ChunkCancelled();

        bool reused = false;
        if ( journal.committed[static_cast<size_t>( index )] )
        {
            const std::string path = tilePath( index ).generic_string();
            TilePayload verified;
            bool verifiedOk = false;
            try
            {
                verified = DiskTileStore::readFile( path );
                // The stored spec must match the identity-derived geometry —
                // a divergence is file corruption, not identity drift (the
                // journal header already gated that).
                const TileSpec expected = tileSpecAt( m_spec.partition, index );
                if ( verified.spec.index != expected.index
                     || verified.spec.totalTiles != expected.totalTiles
                     || verified.spec.xOffset != expected.xOffset
                     || verified.spec.yOffset != expected.yOffset
                     || verified.spec.width != expected.width
                     || verified.spec.height != expected.height
                     || verified.spec.halo != expected.halo
                     || verified.spec.bands != expected.bands )
                {
                    throw ChunkCorruptTile( path ); // recompute below (R4)
                }
                verifiedOk = true;
            }
            catch ( const ChunkCorruptTile & )
            {
                // R4: corrupt/missing committed tile self-heals via compute.
                // (consume runs OUTSIDE this catch: a user consume throwing
                // ChunkCorruptTile must not be misread as tile corruption.)
            }
            if ( verifiedOk )
            {
                cb.consume( verified );
                reused = true;
                ++result.tilesReused;
                telemetry.increment( observability::Counter::CacheHits );
            }
        }
        if ( !reused )
        {
            const TileSpec expected = tileSpecAt( m_spec.partition, index );
            TilePayload payload = cb.compute( expected );
            if ( !payload.pixels
                 || payload.pixels->size() != expected.bufferElementCount() )
                throw std::runtime_error( "resumable tile run: compute returned a buffer of "
                                          + std::to_string(
                                                 payload.pixels ? payload.pixels->size() : 0 )
                                          + " floats, spec needs "
                                          + std::to_string( expected.bufferElementCount() ) );
            payload.spec = expected;
            DiskTileStore::writeFile( tilePath( index ).generic_string(), payload );
            appendCommit( index );
            cb.consume( payload );
            ++result.tilesComputed;
            telemetry.increment( observability::Counter::CacheMisses );
            journal.committed[static_cast<size_t>( index )] = true;
            ++journal.committedCount;
            ++sinceCheckpoint;
            if ( m_config.checkpointIntervalTiles > 0
                 && sinceCheckpoint >= m_config.checkpointIntervalTiles )
            {
                saveCheckpoint( journal.committedCount );
                sinceCheckpoint = 0;
            }
        }
        if ( cb.progress )
            cb.progress( static_cast<double>( index + 1 )
                         / static_cast<double>( m_totalTiles ) );
    }

    saveCheckpoint( journal.committedCount );

    if ( cancel.cancelled() )
        throw ChunkCancelled();

    if ( SICNU_FAULT_POINT( "exec11.publish" ) )
        throw std::runtime_error( "resumable tile run: injected publish failure" );
    cb.publish();
    writeMarker();
    return result;
}

void ResumableTileRun::abandon()
{
    wipeState( /*includeMarker=*/false );
}

void ResumableTileRun::cleanupAfterPublish()
{
    std::error_code ec;
    std::filesystem::remove_all( m_runDir, ec );
    std::filesystem::remove( m_journalPath, ec );
    std::filesystem::remove( m_checkpointPath, ec );
}

} // namespace sicnu::runtime::chunk
