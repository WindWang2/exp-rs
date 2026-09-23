// trace.cpp — see trace.h for the contract.
#include "trace.h"

#include "platform/portable.h"

#include <atomic>
#include <chrono>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace sicnu::runtime::observability::trace
{
namespace
{

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch() )
        .count();
}

std::string jsonEscape( const std::string &text )
{
    std::string out;
    out.reserve( text.size() + 8 );
    for ( const char ch : text )
    {
        switch ( ch )
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ( static_cast<unsigned char>( ch ) < 0x20 )
            {
                char buf[8];
                std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned char>( ch ) );
                out += buf;
            }
            else
            {
                out += ch;
            }
        }
    }
    return out;
}

std::string field( const char *key, const std::string &value, bool &first )
{
    if ( value.empty() )
        return std::string();
    std::string out;
    if ( !first )
        out += ",";
    first = false;
    out += "\"";
    out += key;
    out += "\":\"";
    out += jsonEscape( value );
    out += "\"";
    return out;
}

} // namespace

std::string encodeNdjson( const TraceEvent &event )
{
    bool first = true;
    std::string out = "{\"schema\":\"";
    out += kTraceSchema;
    out += "\",\"ts\":";
    out += std::to_string( event.tsMs );
    first = false;
    out += field( "run", event.run, first );
    out += field( "task", event.task, first );
    out += field( "job", event.job, first );
    out += field( "worker", event.worker, first );
    out += field( "operator", event.op, first );
    out += field( "artifact", event.artifact, first );
    out += field( "event", event.event, first );
    out += field( "phase", event.phase, first );
    out += field( "status", event.status, first );
    out += field( "detail", event.detail, first );
    if ( event.durationUs != 0 )
    {
        if ( !first )
            out += ",";
        first = false;
        out += "\"duration_us\":";
        out += std::to_string( event.durationUs );
    }
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// TraceIdGenerator
// ---------------------------------------------------------------------------

namespace
{
constexpr char kBase32Alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

std::mutex g_generatorMutex;
uint64_t g_lastMs = 0;
uint64_t g_counter = 0;
bool g_deterministic = false;
uint64_t g_lastEpochMs = 0;

/// Extracts 5 bits (group @p g, 0 = least significant) from a 80-bit hi:lo.
uint64_t group80( uint64_t hi, uint64_t lo, int g )
{
    const int shift = g * 5;
    if ( shift < 64 )
        return ( lo >> shift ) & 0x1F;
    return ( hi >> ( shift - 64 ) ) & 0x1F;
}

/// 26-char id: 50-bit ms epoch (10 chars) + 80-bit uniqueness payload
/// (16 chars). The payload is a 15-bit per-process tag (encoded in bits
/// 1..15 — the 5-bit group stride never reads bit 0) above the raw per-ms
/// sequence counter (low 64 bits). The counter must stay order-preserving:
/// XOR with an origin does NOT preserve numeric order (x < y does not
/// imply x^c < y^c), which used to break the "monotonic ids sort
/// ascending" contract within a single millisecond.
std::string encodeId( uint64_t epochMs, uint64_t tag, uint64_t seq )
{
    char out[27];
    uint64_t t = epochMs & ( ( UINT64_C( 1 ) << 50 ) - 1 );
    for ( int i = 9; i >= 0; --i )
    {
        out[i] = kBase32Alphabet[t & 0x1F];
        t >>= 5;
    }
    const uint64_t hi = ( tag << 1 ) & 0xFFFF;
    const uint64_t lo = seq;
    for ( int g = 15; g >= 0; --g )
        out[10 + ( 15 - g )] = kBase32Alphabet[group80( hi, lo, g )];
    out[26] = '\0';
    return std::string( out );
}

uint64_t processOrigin()
{
    // Deterministic test mode: tag 0 keeps ids assertable (payload = seq).
    if ( g_deterministic )
        return 0;
    // 15-bit process tag, pid-dominant so parallel processes on one host
    // disagree even when they start in the same millisecond with the same
    // counter. Constant within a process, so sort order is untouched.
    // Not cryptographic; ids only need process distinction and sortability.
    static const uint64_t tag = [] {
        const uint64_t pid = static_cast<uint64_t>( sicnu::portable::pid() );
        const uint64_t time =
            static_cast<uint64_t>( std::chrono::steady_clock::now().time_since_epoch().count() );
        const uint64_t addr = reinterpret_cast<uint64_t>( &tag );
        return ( ( ( pid * UINT64_C( 0x9E3779B97F4A7C15 ) ) >> 48 ) ^ time ^ ( addr >> 17 ) ) & 0x7FFF;
    }();
    return tag;
}
} // namespace

std::string TraceIdGenerator::next( const char *prefix )
{
    std::lock_guard<std::mutex> lock( g_generatorMutex );
    const uint64_t ms = g_deterministic ? g_lastEpochMs : static_cast<uint64_t>( nowMs() );
    if ( ms != g_lastMs )
    {
        g_lastMs = ms;
        g_counter = 0;
    }
    const uint64_t counter = ++g_counter;
    g_lastEpochMs = ms;
    const std::string id = encodeId( ms, processOrigin(), counter );
    if ( prefix && *prefix )
    {
        std::string full( prefix );
        full += '-';
        full += id;
        return full;
    }
    return id;
}

void TraceIdGenerator::resetForTests( uint64_t seed )
{
    std::lock_guard<std::mutex> lock( g_generatorMutex );
    g_deterministic = true;
    g_lastMs = seed;      // frozen clock
    g_lastEpochMs = seed; // a stable, small "epoch" so ids are assertable
    g_counter = 0;
}

void TraceIdGenerator::restoreAfterTests()
{
    std::lock_guard<std::mutex> lock( g_generatorMutex );
    g_deterministic = false;
}

uint64_t TraceIdGenerator::lastEpochMs()
{
    std::lock_guard<std::mutex> lock( g_generatorMutex );
    return g_lastEpochMs;
}

// ---------------------------------------------------------------------------
// RingTraceSink
// ---------------------------------------------------------------------------

RingTraceSink::RingTraceSink( size_t capacity )
    : m_capacity( capacity ? capacity : 1 )
{
}

void RingTraceSink::write( const TraceEvent &event )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_events.size() >= m_capacity )
        m_events.pop_front();
    m_events.push_back( event );
}

std::vector<TraceEvent> RingTraceSink::snapshot() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return std::vector<TraceEvent>( m_events.begin(), m_events.end() );
}

size_t RingTraceSink::size() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_events.size();
}

void RingTraceSink::clear()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_events.clear();
}

// ---------------------------------------------------------------------------
// FileTraceSink
// ---------------------------------------------------------------------------

FileTraceSink::FileTraceSink( Options options )
    : m_options( std::move( options ) )
{
    std::error_code ec;
    std::filesystem::create_directories( m_options.directory, ec ); // best effort
    if ( m_options.maxFileBytes < 4096 )
        m_options.maxFileBytes = 4096;
    if ( m_options.maxFiles == 0 )
        m_options.maxFiles = 1;
    if ( m_options.queueCapacity < 16 )
        m_options.queueCapacity = 16;
    m_writer = std::thread( [this] { writerLoop(); } );
}

FileTraceSink::~FileTraceSink()
{
    m_stopping.store( true, std::memory_order_release );
    m_queueCv.notify_all();
    if ( m_writer.joinable() )
        m_writer.join();
}

void FileTraceSink::write( const TraceEvent &event )
{
    {
        std::lock_guard<std::mutex> lock( m_queueMutex );
        if ( m_queue.size() >= m_options.queueCapacity )
        {
            m_queue.pop_front(); // drop-oldest; bounded memory
            // Honest accounting: the drop is part of the trace evidence.
            m_dropped.fetch_add( 1, std::memory_order_relaxed );
        }
        m_queue.push_back( event );
    }
    m_queueCv.notify_one();
}

std::string FileTraceSink::describe() const
{
    std::lock_guard<std::mutex> lock( m_io );
    return m_activePath.empty() ? ( m_options.directory + "/" + m_options.baseName + ".ndjson" )
                                : m_activePath;
}

std::string FileTraceSink::activeFilePath() const
{
    std::lock_guard<std::mutex> lock( m_io );
    return m_activePath;
}

void FileTraceSink::writerLoop()
{
    for ( ;; )
    {
        TraceEvent event;
        bool has = false;
        {
            std::unique_lock<std::mutex> lock( m_queueMutex );
            m_queueCv.wait( lock, [this] { return m_stopping.load( std::memory_order_acquire ) || !m_queue.empty(); } );
            if ( !m_queue.empty() )
            {
                event = m_queue.front();
                m_queue.pop_front();
                has = true;
            }
            else if ( m_stopping.load( std::memory_order_acquire ) )
            {
                return;
            }
        }
        if ( !has )
            continue;
        if ( event.tsMs == 0 )
            event.tsMs = nowMs();
        const std::string line = encodeNdjson( event );
        {
            std::lock_guard<std::mutex> ioLock( m_io );
            rotateIfNeeded_locked();
            openFile_locked();
            if ( !m_activePath.empty() )
            {
                std::ofstream file( sicnu::portable::pathFromUtf8( m_activePath ),
                                    std::ios::app | std::ios::binary );
                if ( file.good() )
                {
                    file << line << "\n";
                    m_activeBytes += line.size() + 1;
                }
            }
        }
    }
}

void FileTraceSink::rotateIfNeeded_locked()
{
    if ( m_activeBytes < m_options.maxFileBytes )
        return;
    std::error_code ec;
    // shift .maxFiles-1 → .maxFiles, … .1 → .2
    for ( uint32_t i = m_options.maxFiles; i >= 2; --i )
    {
        const std::string from = m_options.directory + "/" + m_options.baseName + "." +
                                 std::to_string( i - 1 );
        const std::string to = m_options.directory + "/" + m_options.baseName + "." + std::to_string( i );
        // Directory strings hold UTF-8 bytes; the narrow fs::path conversion
        // is ACP on Windows, so every fs call goes through pathFromUtf8.
        if ( std::filesystem::exists( sicnu::portable::pathFromUtf8( from ), ec ) )
            std::filesystem::rename( sicnu::portable::pathFromUtf8( from ),
                                     sicnu::portable::pathFromUtf8( to ), ec );
    }
    const std::string current = m_options.directory + "/" + m_options.baseName + ".ndjson";
    if ( std::filesystem::exists( sicnu::portable::pathFromUtf8( current ), ec ) )
    {
        std::filesystem::rename(
            sicnu::portable::pathFromUtf8( current ),
            sicnu::portable::pathFromUtf8( m_options.directory + "/" + m_options.baseName + ".1" ),
            ec );
    }
    m_activeBytes = 0;
}

void FileTraceSink::openFile_locked()
{
    if ( !m_activePath.empty() )
        return;
    m_activePath = m_options.directory + "/" + m_options.baseName + ".ndjson";
    m_activeBytes = 0;
    std::error_code ec;
    const auto existing =
      std::filesystem::file_size( sicnu::portable::pathFromUtf8( m_activePath ), ec );
    m_activeBytes = ec ? 0 : static_cast<uint64_t>( existing );
}

// ---------------------------------------------------------------------------
// Trace global control
// ---------------------------------------------------------------------------

std::atomic<bool> Trace::s_enabled{ false };
std::mutex Trace::s_mutex;
std::shared_ptr<ITraceSink> Trace::s_sink;

void Trace::install( std::shared_ptr<ITraceSink> sink )
{
    std::lock_guard<std::mutex> lock( s_mutex );
    s_sink = std::move( sink );
    s_enabled.store( static_cast<bool>( s_sink ), std::memory_order_relaxed );
}

std::shared_ptr<ITraceSink> Trace::sink()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    return s_sink;
}

bool Trace::enabled()
{
    return s_enabled.load( std::memory_order_relaxed );
}

void Trace::publish( const TraceEvent &event )
{
    if ( !s_enabled.load( std::memory_order_relaxed ) )
        return;
    std::shared_ptr<ITraceSink> sink;
    {
        std::lock_guard<std::mutex> lock( s_mutex );
        sink = s_sink;
    }
    if ( !sink )
        return;
    TraceEvent stamped = event;
    if ( stamped.tsMs == 0 )
        stamped.tsMs = nowMs();
    sink->write( stamped );
}

std::string installFileSinkFromEnv()
{
    const std::string flag = sicnu::portable::envUtf8( "SICNU_TRACE" );
    if ( flag.empty() || !( flag[0] == '1' || flag[0] == 't' || flag[0] == 'T' ) )
        return std::string();
    FileTraceSink::Options options;
    // The trace directory is path-bearing: read it through the UTF-8 env
    // seam so a non-ASCII directory survives the Windows ACP getenv.
    if ( const std::string dir = sicnu::portable::envUtf8( "SICNU_TRACE_DIR" ); !dir.empty() )
        options.directory = dir;
    if ( options.directory.empty() )
    {
        std::error_code ec;
        auto tmpRoot = std::filesystem::temp_directory_path( ec );
        if ( ec )
            return std::string(); // no usable temp dir: stay disabled, never throw
        // #1178: never path::string() (ANSI on Windows) — mirror env_doctor's
        // u8PathString so non-ASCII usernames keep a usable trace directory.
        const std::filesystem::path traceDir = tmpRoot / "sicnu-trace";
        try
        {
            const std::u8string u8 = traceDir.generic_u8string();
            options.directory.assign( reinterpret_cast<const char *>( u8.data() ), u8.size() );
        }
        catch ( ... )
        {
            return std::string();
        }
    }
    const std::string maxMb = sicnu::portable::envUtf8( "SICNU_TRACE_MAX_MB" );
    if ( !maxMb.empty() )
    {
        const unsigned long long mb = std::strtoull( maxMb.c_str(), nullptr, 10 );
        if ( mb > 0 && mb <= 512 )
            options.maxFileBytes = mb * 1024ull * 1024ull;
    }
    auto sink = std::make_shared<FileTraceSink>( options );
    Trace::install( sink );
    return sink->describe();
}

} // namespace sicnu::runtime::observability::trace
