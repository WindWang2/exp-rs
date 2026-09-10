// trace.cpp — see trace.h for the contract.
#include "trace.h"

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

/// 26-char id: 50-bit ms epoch (10 chars) + 80-bit uniqueness (16 chars).
/// Uniqueness payload = constant per-process origin tag (high 16 bits) and
/// seq XOR origin-low-64 (low 64 bits) — XOR by a constant preserves order,
/// so ids from the same process sort by (ms, seq).
std::string encodeId( uint64_t epochMs, uint64_t origin, uint64_t seq )
{
    char out[27];
    uint64_t t = epochMs & ( ( UINT64_C( 1 ) << 50 ) - 1 );
    for ( int i = 9; i >= 0; --i )
    {
        out[i] = kBase32Alphabet[t & 0x1F];
        t >>= 5;
    }
    const uint64_t hi = ( origin >> 48 ) & 0xFFFF;
    const uint64_t lo = seq ^ origin;
    for ( int g = 15; g >= 0; --g )
        out[10 + ( 15 - g )] = kBase32Alphabet[group80( hi, lo, g )];
    out[26] = '\0';
    return std::string( out );
}

uint64_t processOrigin()
{
    // Deterministic test mode: origin 0 keeps ids assertable (payload = seq).
    if ( g_deterministic )
        return 0;
    // Process-unique random origin: address-space + clock mixing.
    // Not cryptographic; ids only need process uniqueness and sortability.
    static const uint64_t origin = [] {
        const uint64_t time =
            static_cast<uint64_t>( std::chrono::steady_clock::now().time_since_epoch().count() );
        const uint64_t addr = reinterpret_cast<uint64_t>( &origin );
        return time ^ ( addr << 1 ) ^ UINT64_C( 0x9E3779B97F4A7C15 );
    }();
    return origin;
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
            m_queue.pop_front(); // drop-oldest; bounded memory
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
                std::ofstream file( m_activePath, std::ios::app | std::ios::binary );
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
        if ( std::filesystem::exists( from, ec ) )
            std::filesystem::rename( from, to, ec );
    }
    const std::string current = m_options.directory + "/" + m_options.baseName + ".ndjson";
    if ( std::filesystem::exists( current, ec ) )
    {
        std::filesystem::rename(
            current, m_options.directory + "/" + m_options.baseName + ".1", ec );
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
    const auto existing = std::filesystem::file_size( m_activePath, ec );
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

void Trace::emit( const TraceEvent &event )
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
    const char *flag = std::getenv( "SICNU_TRACE" );
    if ( !flag || !( flag[0] == '1' || flag[0] == 't' || flag[0] == 'T' ) )
        return std::string();
    FileTraceSink::Options options;
    if ( const char *dir = std::getenv( "SICNU_TRACE_DIR" ) )
        options.directory = dir;
    if ( options.directory.empty() )
        options.directory = ( std::filesystem::temp_directory_path() / "sicnu-trace" ).string();
    if ( const char *maxMb = std::getenv( "SICNU_TRACE_MAX_MB" ) )
    {
        const unsigned long long mb = std::strtoull( maxMb, nullptr, 10 );
        if ( mb > 0 && mb <= 512 )
            options.maxFileBytes = mb * 1024ull * 1024ull;
    }
    auto sink = std::make_shared<FileTraceSink>( options );
    Trace::install( sink );
    return sink->describe();
}

} // namespace sicnu::runtime::observability::trace
