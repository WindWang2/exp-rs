// scratch_registry.cpp — see scratch_registry.h for the lease contract.
#include "scratch_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>
#include <vector>

#if !defined( _WIN32 )
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sicnu::runtime::chunk
{

namespace
{
constexpr const char *kPartSuffix = ".part";

/// FNV-1a 64-bit content digest — a fast CORRUPTION tripwire for scratch
/// files, not a security primitive. Spilled tiles are re-verified after a
/// crash before reuse; a failed verify means "recompute the tile"
/// (fail-closed, no correctness edge).
std::uint64_t fnv1a( const void *data, std::size_t size )
{
    const auto *bytes = static_cast<const std::uint8_t *>( data );
    std::uint64_t hash = 1469598103934665603ull;
    for ( std::size_t i = 0; i < size; ++i )
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string sidecarPath( const std::string &path )
{
    return path + ".digest";
}

void fsyncPath( const std::string &path, bool directory )
{
#if !defined( _WIN32 )
    const int flags = directory ? ( O_RDONLY | O_DIRECTORY ) : O_RDONLY;
    const int fd = ::open( path.c_str(), flags );
    if ( fd >= 0 )
    {
        ::fsync( fd );
        ::close( fd );
    }
#else
    (void)path;
    (void)directory; // Windows durability follows the checkpoint precedent:
                     // rename is the atomicity boundary, fsync is best-effort.
#endif
}

/// Best-effort parse of the `.digest` sidecar; false on any malformed input.
bool readDigestSidecar( const std::string &path, std::uint64_t &hash, std::uint64_t &size )
{
    std::ifstream in( sidecarPath( path ), std::ios::binary );
    std::string hashHex;
    if ( !( in >> hashHex ) )
        return false;
    if ( hashHex.size() > 16 )
        return false;
    for ( const char c : hashHex )
        if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
            return false;
    hash = std::stoull( hashHex, nullptr, 16 );
    if ( !( in >> size ) )
        return false;
    return true;
}
} // namespace

// ── ScratchLease::Entry ────────────────────────────────────────────────────

struct ScratchLease::Entry
{
    ScratchRegistry *registry = nullptr; // nulled when the registry dies
    std::string path;                    ///< provisional `<name>.part` path (writable)
    std::string finalPath;               ///< post-finalize name (no suffix)
    std::string runId;
    std::uint64_t bytes = 0;
    std::atomic<bool> finalized{ false };
    std::atomic<bool> released{ false };
};

const std::string ScratchLease::k_empty;
ScratchLease::ScratchLease( std::shared_ptr<Entry> impl ) : m_impl( std::move( impl ) ) {}

const std::string &ScratchLease::path() const
{
    return m_impl ? m_impl->path : k_empty;
}

const std::string &ScratchLease::finalPath() const
{
    return m_impl ? m_impl->finalPath : k_empty;
}

std::uint64_t ScratchLease::bytes() const
{
    return m_impl ? m_impl->bytes : 0;
}

bool ScratchLease::isFinalized() const
{
    return m_impl && m_impl->finalized.load();
}

void ScratchLease::sealDigest() const
{
    if ( !m_impl )
        return;
    std::error_code ec;
    const auto size = std::filesystem::file_size( m_impl->path, ec );
    if ( ec )
        return;
    std::vector<char> buffer( static_cast<std::size_t>( size ) );
    std::ifstream in( m_impl->path, std::ios::binary );
    in.read( buffer.data(), buffer.size() );
    if ( static_cast<std::size_t>( in.gcount() ) != buffer.size() )
        return;
    std::ofstream out( sidecarPath( m_impl->path ), std::ios::binary | std::ios::trunc );
    out << std::hex << fnv1a( buffer.data(), buffer.size() ) << "\n"
        << std::dec << size << "\n";
}

const std::string &ScratchLease::finalize() const
{
    if ( !m_impl )
        return k_empty;
    bool expected = false;
    if ( m_impl->finalized.compare_exchange_strong( expected, true ) )
    {
        // fsync the file and its directory, then rename — the checkpoint
        // atomicity family (tmp + durable + atomic replace).
        fsyncPath( m_impl->path, /*directory=*/false );
        if ( auto dir = std::filesystem::path( m_impl->path ).parent_path(); !dir.empty() )
            fsyncPath( dir.string(), /*directory=*/true );
        std::error_code ec;
        std::filesystem::rename( m_impl->path, m_impl->finalPath, ec );
        if ( !ec )
        {
            // Rename is atomic; the sidecar follows with a non-atomic move —
            // a crash between the two leaves the final file without a
            // digest, which verifyDigest() answers fail-closed (recompute).
            std::error_code ec2;
            std::filesystem::rename( sidecarPath( m_impl->path ),
                                     sidecarPath( m_impl->finalPath ), ec2 );
        }
        // A failed main rename is left as-is: a racing finalize may have
        // won, or the next sweep/retry converges; never delete content.
    }
    return m_impl->finalPath;
}

bool ScratchLease::verifyDigest() const
{
    if ( !m_impl )
        return false;
    std::error_code ec;
    if ( !std::filesystem::exists( m_impl->finalPath, ec ) )
        return false;
    std::uint64_t expectedHash = 0;
    std::uint64_t expectedSize = 0;
    if ( !readDigestSidecar( m_impl->finalPath, expectedHash, expectedSize ) )
        return false;
    const auto size = std::filesystem::file_size( m_impl->finalPath, ec );
    if ( ec || size != expectedSize )
        return false;
    std::vector<char> buffer( static_cast<std::size_t>( size ) );
    std::ifstream content( m_impl->finalPath, std::ios::binary );
    content.read( buffer.data(), buffer.size() );
    if ( static_cast<std::size_t>( content.gcount() ) != buffer.size() )
        return false;
    return fnv1a( buffer.data(), buffer.size() ) == expectedHash;
}

void ScratchLease::Deleter::operator()( Entry *entry ) const
{
    if ( !entry )
        return;
    ScratchRegistry *registry = entry->registry;
    if ( registry )
        registry->releaseEntry( entry ); // takes ownership of `entry`
    else
        delete entry; // registry died first: no accounting to undo
}

// ── ScratchRegistry ────────────────────────────────────────────────────────

ScratchRegistry::ScratchRegistry( Config config ) : m_config( std::move( config ) )
{
    if ( m_config.root.empty() )
    {
        std::error_code ec;
        m_rootPath = std::filesystem::temp_directory_path( ec );
        if ( ec )
            m_rootPath = std::filesystem::path( "/tmp" );
        m_rootPath /= "sicnu-scratch";
    }
    else
    {
        m_rootPath = m_config.root;
    }
    std::error_code ec;
    std::filesystem::create_directories( m_rootPath, ec );
}

ScratchRegistry::~ScratchRegistry()
{
    // Detach every live lease: its release path must not touch this (dying)
    // registry. Files stay on disk; the stale sweep owns them from here on.
    std::lock_guard<std::mutex> lock( m_mutex );
    for ( auto &weak : m_liveEntries )
    {
        if ( auto entry = weak.lock(); entry && !entry->released.load() )
            entry->registry = nullptr;
    }
    m_liveEntries.clear();
}

std::string ScratchRegistry::root() const
{
    return m_rootPath.string();
}

ScratchLease ScratchRegistry::acquire( const std::string &runId, const std::string &stem,
                                       std::uint64_t bytes )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_config.budgetBytes != 0 && m_outstandingTotal + bytes > m_config.budgetBytes )
        throw ScratchBudgetExceeded( bytes, m_outstandingTotal, m_config.budgetBytes );

    const auto runDir = m_rootPath / runId;
    std::error_code ec;
    std::filesystem::create_directories( runDir, ec );

    const std::string name = stem + "-" + std::to_string( m_nextFileCounter++ );
    const auto provisional = runDir / ( name + kPartSuffix );
    const auto final = runDir / name;

    {
        std::ofstream create( provisional, std::ios::binary | std::ios::trunc );
        if ( !create )
            throw std::runtime_error( "scratch acquire: cannot create " + provisional.string() );
    }

    // Raw new + custom deleter: the shared_ptr refcount IS the lease
    // refcount, and the LAST release must unaccount the bytes (the default
    // delete would leak the accounting and the provisional file).
    ScratchLease::Entry *rawEntry = new ScratchLease::Entry();
    rawEntry->registry = this;
    rawEntry->path = provisional.string();
    rawEntry->finalPath = final.string();
    rawEntry->runId = runId;
    rawEntry->bytes = bytes;
    std::shared_ptr<ScratchLease::Entry> entry( rawEntry, ScratchLease::Deleter{} );
    m_liveEntries.emplace_back( entry );
    if ( m_liveEntries.size() >= 256 )
    {
        // Amortized prune of expired weak refs (released leases).
        m_liveEntries.erase(
            std::remove_if( m_liveEntries.begin(), m_liveEntries.end(),
                            []( const auto &weak ) { return weak.expired(); } ),
            m_liveEntries.end() );
    }

    m_outstandingTotal += bytes;
    m_outstandingByRun[runId] += bytes;
    return ScratchLease( std::move( entry ) );
}

void ScratchRegistry::unaccountLocked( const std::string &runId, std::uint64_t bytes )
{
    m_outstandingTotal = bytes >= m_outstandingTotal ? 0 : m_outstandingTotal - bytes;
    auto it = m_outstandingByRun.find( runId );
    if ( it != m_outstandingByRun.end() )
    {
        it->second = bytes >= it->second ? 0 : it->second - bytes;
        if ( it->second == 0 )
            m_outstandingByRun.erase( it );
    }
}

void ScratchRegistry::releaseEntry( ScratchLease::Entry *entry )
{
    const bool wasReleased = entry->released.exchange( true );
    if ( !wasReleased && entry->registry )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        unaccountLocked( entry->runId, entry->bytes );
        // Delete ONLY the provisional file: a finalized file may still be
        // referenced by a disk-backed tile consumer and dies with the run
        // sweep (or an explicit cleanup), never mid-stream.
        if ( !entry->finalized.load() )
        {
            std::error_code ec;
            std::filesystem::remove( entry->path, ec );
            std::filesystem::remove( sidecarPath( entry->path ), ec );
        }
    }
    delete entry;
}

std::uint64_t ScratchRegistry::outstandingBytes() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_outstandingTotal;
}

std::uint64_t ScratchRegistry::outstandingBytes( const std::string &runId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_outstandingByRun.find( runId );
    return it == m_outstandingByRun.end() ? 0 : it->second;
}

std::size_t ScratchRegistry::sweepStale( const std::string &root,
                                         std::chrono::milliseconds age )
{
    std::size_t removed = 0;
    std::error_code ec;
    const auto rootDir = std::filesystem::path( root );
    if ( !std::filesystem::exists( rootDir, ec ) )
        return 0;
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto cutoff = std::chrono::duration_cast<std::filesystem::file_time_type::duration>( age );
    for ( const auto &entry : std::filesystem::directory_iterator( rootDir, ec ) )
    {
        if ( ec || !entry.is_directory() )
            continue;
        const auto lastWrite = entry.last_write_time( ec );
        if ( ec )
            continue;
        if ( now - lastWrite > cutoff )
        {
            std::filesystem::remove_all( entry.path(), ec );
            if ( !ec )
                ++removed;
        }
    }
    return removed;
}

} // namespace sicnu::runtime::chunk
