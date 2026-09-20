// perf_observatory.h — Cross-module performance & memory observatory harness.
//
// Shared measurement core for the perf-observatory track. Deliberately
// dependency-light: standard C++20, jsoncpp and the OS' own resource APIs
// only, so both the Qt-free I/O target and the full execution-plane target
// can include it unchanged.
//
// Contract (mirrors and extends `execution-bench/1` from
// test_execution_benchmarks.cpp):
//
//   * Every workload records STRUCTUAL indicators (input sizes, request
//     counts, cache hits/misses) plus measured wall/cpu/memory/IO.
//   * Structural indicators are machine-independent: two runs on the same
//     build MUST agree. Timings are machine-relative and are never asserted
//     equal across runs.
//   * An unavailable metric is recorded as null with an explicit reason,
//     never as a misleading zero.
//   * Nothing is written into the repository: records land in a caller
//     supplied directory or a process-private temporary directory.
//
// Size selection: SICNU_OBS_SCALE=small|mid|scale (default small, ctest
// friendly). Determinism: every fixture uses a baked-in LCG seed.
#ifndef SICNU_TESTS_PERF_PERF_OBSERVATORY_H
#define SICNU_TESTS_PERF_PERF_OBSERVATORY_H

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined( _WIN32 )
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <psapi.h>
#elif defined( __APPLE__ )
  #include <mach/mach.h>
#elif defined( __linux__ )
  #include <sys/resource.h>
  #include <unistd.h>
#endif

namespace sicnu::testing::perf
{

//------------------------------------------------------------------------------
// Deterministic fixtures
//------------------------------------------------------------------------------

/// Fixed-seed linear congruential generator (same closure as the existing
/// harnesses) so every workload is byte-reproducible offline.
class Lcg
{
  public:
    explicit Lcg( std::uint32_t seed = 0x12345678u ) : m_state( seed ) {}

    void seed( std::uint32_t s ) { m_state = s; }

    /// [0,1)
    double unit()
    {
        m_state = m_state * 1103515245u + 12345u;
        return static_cast<double>( m_state >> 8 ) / static_cast<double>( 0xFFFFFF );
    }

    /// [min,max)
    double range( double lo, double hi ) { return lo + unit() * ( hi - lo ); }

    std::uint32_t nextU32()
    {
        m_state = m_state * 1103515245u + 12345u;
        return m_state;
    }

    std::uint32_t state() const { return m_state; }

  private:
    std::uint32_t m_state = 0;
};

//------------------------------------------------------------------------------
// Scale selection
//------------------------------------------------------------------------------

enum class Scale { Small, Mid, Large };

inline const char *scaleName( Scale s )
{
    switch ( s )
    {
        case Scale::Mid:
            return "mid";
        case Scale::Large:
            return "scale";
        case Scale::Small:
        default:
            return "small";
    }
}

inline Scale scaleFromEnv()
{
    const char *env = std::getenv( "SICNU_OBS_SCALE" );
    if ( !env )
        return Scale::Small;
    const std::string v( env );
    if ( v == "mid" )
        return Scale::Mid;
    if ( v == "scale" || v == "large" )
        return Scale::Large;
    return Scale::Small;
}

/// Three-point size ladder for one workload dimension: small / mid / large.
/// Baked into every workload so a baseline record always states which rung
/// produced its numbers.
struct Ladder
{
    int small = 0;
    int mid = 0;
    int large = 0;

    int pick( Scale s ) const
    {
        switch ( s )
        {
            case Scale::Mid:
                return mid > 0 ? mid : small;
            case Scale::Large:
                return large > 0 ? large : ( mid > 0 ? mid : small );
            case Scale::Small:
            default:
                return small;
        }
    }
};

//------------------------------------------------------------------------------
// Portable resource sampling
//------------------------------------------------------------------------------

/// Resident set size of this process, in MiB. 0 when the platform counter is
/// unavailable (the caller records that fact, see IoSnapshot).
inline unsigned currentRssMb()
{
#if defined( _WIN32 )
    PROCESS_MEMORY_COUNTERS pmc;
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
        return static_cast<unsigned>( pmc.WorkingSetSize >> 20 );
    return 0;
#elif defined( __APPLE__ )
    const task_port_t task = mach_task_self();
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if ( task_info( task, MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>( &info ), &count )
         == KERN_SUCCESS )
        return static_cast<unsigned>( info.resident_size >> 20 );
    return 0;
#elif defined( __linux__ )
    std::ifstream f( "/proc/self/status" );
    std::string line;
    while ( std::getline( f, line ) )
    {
        if ( line.compare( 0, 6, "VmRSS:" ) == 0 )
        {
            long long kb = 0;
            std::istringstream is( line.substr( 6 ) );
            is >> kb;
            return static_cast<unsigned>( kb / 1024 );
        }
    }
    return 0;
#else
    return 0;
#endif
}

/// CPU time consumed by this process (user + system), in ms.
inline double currentCpuMs()
{
#if defined( _WIN32 )
    FILETIME creation, exit, kernel, user;
    if ( GetProcessTimes( GetCurrentProcess(), &creation, &exit, &kernel, &user ) )
    {
        auto toMs = []( const FILETIME &ft ) {
            ULARGE_INTEGER v;
            v.LowPart = ft.dwLowDateTime;
            v.HighPart = ft.dwHighDateTime;
            return static_cast<double>( v.QuadPart ) / 10000.0; // 100ns units -> ms
        };
        return toMs( kernel ) + toMs( user );
    }
    return 0.0;
#elif defined( __APPLE__ ) || defined( __linux__ )
    rusage usage;
    if ( getrusage( RUSAGE_SELF, &usage ) == 0 )
    {
        return static_cast<double>( usage.ru_utime.tv_sec ) * 1000.0
               + static_cast<double>( usage.ru_utime.tv_usec ) / 1000.0
               + static_cast<double>( usage.ru_stime.tv_sec ) * 1000.0
               + static_cast<double>( usage.ru_stime.tv_usec ) / 1000.0;
    }
    return 0.0;
#else
    return 0.0;
#endif
}

/// Process-wide byte/op counters. `available == false` means the platform
/// exposes no such counter and `reason` says why — the record then stores null
/// for those fields instead of a fabricated zero.
struct IoSample
{
    bool available = false;
    const char *reason = nullptr;
    long long readBytes = 0;
    long long writeBytes = 0;
    long long readOps = 0;
    long long writeOps = 0;
};

inline IoSample readIoCounters()
{
    IoSample s;
#if defined( _WIN32 )
    IO_COUNTERS c;
    if ( GetProcessIoCounters( GetCurrentProcess(), &c ) )
    {
        s.available = true;
        s.reason = nullptr;
        s.readBytes = static_cast<long long>( c.ReadTransferCount );
        s.writeBytes = static_cast<long long>( c.WriteTransferCount );
        s.readOps = static_cast<long long>( c.ReadOperationCount );
        s.writeOps = static_cast<long long>( c.WriteOperationCount );
        return s;
    }
    s.reason = "GetProcessIoCounters failed";
    return s;
#elif defined( __linux__ )
    std::ifstream f( "/proc/self/io" );
    std::string line;
    while ( std::getline( f, line ) )
    {
        auto value = [&line]( const char *key ) -> long long {
            const size_t n = std::strlen( key );
            if ( line.compare( 0, n, key ) == 0 )
                return std::atoll( line.c_str() + n + 1 );
            return -1;
        };
        if ( const long long v = value( "read_bytes" ); v >= 0 )
            s.readBytes = v;
        else if ( const long long v = value( "write_bytes" ); v >= 0 )
            s.writeBytes = v;
        else if ( const long long v = value( "syscr" ); v >= 0 )
            s.readOps = v;
        else if ( const long long v = value( "syscw" ); v >= 0 )
            s.writeOps = v;
    }
    s.available = true;
    return s;
#else
    s.reason = "no process IO counter on this platform";
    return s;
#endif
}

/// Polls peak RSS on a helper thread while a workload runs. The sample is a
/// coarse watermark by construction; it is used as a *bound* evidence
/// (does the workload stay O(page) or does it balloon?), never as a precise
/// number.
class PeakRssTracker
{
  public:
    void start() { m_run.store( true ); m_thread = std::thread( &PeakRssTracker::poll, this ); }
    unsigned stop()
    {
        m_run.store( false );
        if ( m_thread.joinable() )
            m_thread.join();
        return m_peak.load();
    }
    ~PeakRssTracker()
    {
        if ( m_thread.joinable() )
        {
            m_run.store( false );
            m_thread.join();
        }
    }

  private:
    void poll()
    {
        while ( m_run.load() )
        {
            const unsigned cur = currentRssMb();
            unsigned prev = m_peak.load();
            while ( cur > prev && !m_peak.compare_exchange_weak( prev, cur ) )
            {
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        }
    }

    std::atomic<bool> m_run{ false };
    std::atomic<unsigned> m_peak{ 0 };
    std::thread m_thread;
};

//------------------------------------------------------------------------------
// Sample
//------------------------------------------------------------------------------

/// Structural, machine-independent counts a workload produces.
struct Counts
{
    long long tasksDispatched = 0;
    long long pagesRequested = 0;   ///< store/DB query requests (paged calls)
    long long cacheHits = 0;
    long long cacheMisses = 0;
    long long filesWritten = 0;     ///< artifacts the harness produced
    long long rowsMaterialized = 0; ///< structural memory unit (rows in memory)

    Json::Value toJson() const
    {
        Json::Value o( Json::objectValue );
        o["tasks_dispatched"] = static_cast<Json::Int64>( tasksDispatched );
        o["pages_requested"] = static_cast<Json::Int64>( pagesRequested );
        o["cache_hits"] = static_cast<Json::Int64>( cacheHits );
        o["cache_misses"] = static_cast<Json::Int64>( cacheMisses );
        o["files_written"] = static_cast<Json::Int64>( filesWritten );
        o["rows_materialized"] = static_cast<Json::Int64>( rowsMaterialized );
        return o;
    }
};

/// One measured workload.
struct Sample
{
    double wallMs = 0.0;         ///< steady-clock wall time
    double cpuMs = 0.0;          ///< user+system CPU of the process
    unsigned peakRssMb = 0;      ///< peak RSS watermark during the workload
    unsigned baseRssMb = 0;      ///< RSS immediately before the workload
    long long readBytes = 0;
    long long writeBytes = 0;
    long long readOps = 0;
    long long writeOps = 0;
    bool ioAvailable = false;
    const char *ioReason = nullptr;
    Counts counts;
    Json::Value scale = Json::objectValue;
    Json::Value extra = Json::objectValue;

    /// Peak-RSS delta over the pre-workload baseline. Absolute RSS is
    /// meaningless across runs; the delta is not.
    ///
    /// `false` means the watermark never rose above the baseline — the sample
    /// resolution is 1 MiB and the poller wakes every 2 ms, so a sub-MiB or
    /// sub-interval allocation is invisible. Reporting 0 there would be the fake
    /// zero this schema promises never to write, so it is null with a reason.
    unsigned peakRssDeltaMb() const { return peakRssMb > baseRssMb ? peakRssMb - baseRssMb : 0; }

    Json::Value toJson() const
    {
        Json::Value m( Json::objectValue );
        m["wall_ms"] = wallMs;
        // Process CPU across ALL threads, user + system, so on a multi-core
        // lane it can exceed wall_ms. It is a delta and therefore meaningful for
        // one workload, but it is not comparable across machines or against a
        // single core's worth of work.
        m["cpu_ms"] = cpuMs;
        const unsigned rssDelta = peakRssDeltaMb();
        if ( rssDelta > 0 )
        {
            m["peak_rss_mb"] = static_cast<Json::UInt>( rssDelta );
            m["peak_rss_unavailable_reason"] = Json::Value::null;
        }
        else
        {
            m["peak_rss_mb"] = Json::Value::null;
            m["peak_rss_unavailable_reason"] =
                Json::Value( "watermark never rose above the 1 MiB / 2 ms sample "
                             "resolution; see docs/perf-observatory.md" );
        }
        m["peak_rss_baseline_mb"] = static_cast<Json::UInt>( baseRssMb );
        if ( ioAvailable )
        {
            m["read_bytes"] = static_cast<Json::Int64>( readBytes );
            m["write_bytes"] = static_cast<Json::Int64>( writeBytes );
            m["read_ops"] = static_cast<Json::Int64>( readOps );
            m["write_ops"] = static_cast<Json::Int64>( writeOps );
        }
        else
        {
            m["read_bytes"] = Json::Value::null;
            m["write_bytes"] = Json::Value::null;
            m["read_ops"] = Json::Value::null;
            m["write_ops"] = Json::Value::null;
        }
        Json::Value io( Json::objectValue );
        io["available"] = ioAvailable;
        io["unavailable_reason"] = ioReason ? Json::Value( ioReason ) : Json::Value::null;
        m["io"] = io;
        return m;
    }
};

/// Runs `fn` and returns the measured sample. `fn` must fill `s` with any
/// structural counts it wants recorded.
template <typename Fn>
Sample measure( Fn fn )
{
    Sample s;
    const unsigned baseMb = currentRssMb();
    const IoSample io0 = readIoCounters();
    const double cpu0 = currentCpuMs();
    const auto wall0 = std::chrono::steady_clock::now();

    PeakRssTracker tracker;
    tracker.start();
    fn( s );
    const unsigned peak = tracker.stop();

    const auto wall1 = std::chrono::steady_clock::now();
    s.wallMs = std::chrono::duration<double, std::milli>( wall1 - wall0 ).count();
    s.cpuMs = currentCpuMs() - cpu0;
    s.peakRssMb = peak;
    s.baseRssMb = baseMb;
    s.ioAvailable = io0.available;
    s.ioReason = io0.reason;
    if ( io0.available )
    {
        const IoSample io1 = readIoCounters();
        s.readBytes = io1.readBytes - io0.readBytes;
        s.writeBytes = io1.writeBytes - io0.writeBytes;
        s.readOps = io1.readOps - io0.readOps;
        s.writeOps = io1.writeOps - io0.writeOps;
    }
    return s;
}

//------------------------------------------------------------------------------
// Complexity
//------------------------------------------------------------------------------

/// One rung of a two-point (or more) complexity ladder.
struct ComplexityPoint
{
    int n = 0;
    double ms = 0.0;

    Json::Value toJson() const
    {
        Json::Value o( Json::objectValue );
        o["n"] = n;
        o["ms"] = ms;
        return o;
    }
};

/// Empirical base-2 exponent `log2(t(n2) / t(n1))` for a doubling of n.
/// 1.0 means linear cost, 2.0 quadratic, 0.5 sub-linear. Negative results are
/// clamped to 0 — noise can only over-report, and a negative exponent is
/// reported as sub-linear rather than an error.
inline double complexityExponent( double ms1, double ms2 )
{
    if ( ms1 <= 0.0 || ms2 <= 0.0 )
        return 0.0;
    const double ratio = ms2 / ms1;
    return std::max( 0.0, std::log2( ratio ) );
}

inline Json::Value complexityToJson( const std::vector<ComplexityPoint> &points )
{
    Json::Value o( Json::objectValue );
    Json::Value arr( Json::arrayValue );
    for ( const auto &p : points )
        arr.append( p.toJson() );
    o["points"] = arr;
    o["exponent"] = points.size() >= 2
                        ? complexityExponent( points.front().ms, points.back().ms )
                        : Json::Value::null;
    const double e = o["exponent"].asDouble();
    // Always present, including for a single point and for a clamped/0
    // exponent: a consumer reading `model` must never have to special-case its
    // absence, because "sub-linear" is a measurement, not a missing field.
    const char *model = "unknown";
    if ( points.size() >= 2 )
        model = e <= 0.0 ? "sub-linear"
                         : ( e < 1.35 ? "linear" : ( e < 1.65 ? "near-linear" : "super-linear" ) );
    o["model"] = model;
    return o;
}

//------------------------------------------------------------------------------
// Environment & record
//------------------------------------------------------------------------------

struct Environment
{
    std::string os;
    std::string compiler;
    std::string buildType;
    std::string cpuModel;
    unsigned cores = 0;
};

inline const Environment &environment()
{
    static const Environment env = [] {
        Environment e;
#if defined( _WIN32 )
        e.os = "windows";
        e.compiler = "MSVC " + std::to_string( _MSC_VER );
#elif defined( __APPLE__ )
        e.os = "macos";
        e.compiler = "clang " + std::to_string( __clang_major__ );
#elif defined( __linux__ )
        e.os = "linux";
        e.compiler = "gcc " + std::to_string( __GNUC__ );
#else
        e.os = "unknown";
#endif
#if defined( NDEBUG )
        e.buildType = "Release";
#else
        e.buildType = "Debug";
#endif
        e.cores = std::max( 1u, std::thread::hardware_concurrency() );
#if defined( __linux__ )
        std::ifstream f( "/proc/cpuinfo" );
        std::string line;
        while ( std::getline( f, line ) )
        {
            if ( line.compare( 0, 10, "model name" ) == 0 )
            {
                e.cpuModel = line.substr( line.find( ':' ) + 2 );
                break;
            }
        }
#endif
        return e;
    }();
    return env;
}

inline Json::Value environmentToJson()
{
    const Environment &e = environment();
    Json::Value o( Json::objectValue );
    o["os"] = e.os;
    o["compiler"] = e.compiler;
    o["build_type"] = e.buildType;
    if ( e.cpuModel.empty() )
        o["cpu_model"] = Json::Value::null;
    else
        o["cpu_model"] = e.cpuModel;
    o["cores"] = e.cores;
    return o;
}

/// Output directory for observatory records.
/// Precedence: SICNU_OBS_OUT (caller supplied) > system temp directory.
/// Never the repository. Creates the directory when needed.
inline std::string outputDir()
{
    const char *out = std::getenv( "SICNU_OBS_OUT" );
    if ( out && *out )
        return std::string( out );
    const char *tmp = std::getenv( "TMPDIR" );
    if ( !tmp || !*tmp )
        tmp = std::getenv( "TEMP" );
    if ( !tmp || !*tmp )
        tmp = std::getenv( "TMP" );
    if ( !tmp || !*tmp )
        tmp = ".";
    return std::string( tmp ) + "/sicnu-perf-observatory";
}

//------------------------------------------------------------------------------
// Fixtures
//------------------------------------------------------------------------------

/// A process-private scratch directory that removes itself on destruction.
///
/// Needed because the Qt-free I/O target cannot use QTemporaryDir, and because
/// a counter-suffixed directory that is never removed accumulates across runs
/// (this observatory's own earlier version left megabytes of synthetic rasters
/// in %TEMP% forever). Fixtures therefore live in a directory that dies with
/// the workload, and never in the repository.
class ScratchDir
{
  public:
    ScratchDir() : m_path( makePath() )
    {
        std::error_code ec;
        std::filesystem::create_directories( m_path, ec );
    }

    ~ScratchDir()
    {
        std::error_code ec;
        std::filesystem::remove_all( m_path, ec );
    }

    ScratchDir( const ScratchDir & ) = delete;
    ScratchDir &operator=( const ScratchDir & ) = delete;

    const std::string &path() const { return m_path; }
    std::string file( const std::string &name ) const { return m_path + "/" + name; }

  private:
    static std::string makePath()
    {
        static std::atomic<unsigned> counter{ 0 };
        return outputDir() + "/scratch-" + std::to_string( counter.fetch_add( 1 ) );
    }

    std::string m_path;
};

inline std::string isoTimestamp()
{
    const std::time_t t = std::time( nullptr );
    std::tm tmUtc{};
#if defined( _WIN32 )
    gmtime_s( &tmUtc, &t );
#else
    gmtime_r( &t, &tmUtc );
#endif
    char buf[40];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", &tmUtc );
    return std::string( buf );
}


inline void writeRecord( const std::string &outDir, const std::string &workload,
                         const Sample &sample, const Json::Value &complexity = Json::Value(),
                         const Json::Value &structural = Json::objectValue )
{
    Json::Value root( Json::objectValue );
    root["schema"] = "sicnu-perf-observatory/1";
    root["generated_at"] = isoTimestamp();
    root["workload"] = workload;
    root["environment"] = environmentToJson();
    root["scale"] = sample.scale;
    root["measurement"] = sample.toJson();
    root["counts"] = sample.counts.toJson();
    if ( !complexity.isNull() )
        root["complexity"] = complexity;
    root["structural"] = structural;
    root["extra"] = sample.extra;

    std::string dir = outDir;
    if ( dir.empty() )
        dir = outputDir();
    // std::filesystem, not std::system: a shell per record costs a process each
    // time and makes the destination path parseable as shell syntax (a quote, a
    // `%`, or a delayed-expansion `!` in a Windows temp path breaks out of the
    // quoting). Best effort here; a failure surfaces as an open failure below.
    std::error_code ec;
    std::filesystem::create_directories( dir, ec );
    const std::string path = dir + "/" + workload + ".json";
    std::ofstream f( path, std::ios::binary | std::ios::trunc );
    if ( !f )
        return;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    f << Json::writeString( builder, root );
    f << "\n";
}

/// Convenience: run a workload through measure() and write one record.
template <typename Fn>
Sample runAndRecord( const std::string &outDir, const std::string &workload, const Json::Value &scale,
                     Fn fn, const Json::Value &complexity = Json::Value(),
                     const Json::Value &structural = Json::objectValue )
{
    Sample s = measure( fn );
    s.scale = scale;
    writeRecord( outDir, workload, s, complexity, structural );
    return s;
}

//------------------------------------------------------------------------------
// Assertion helpers
//------------------------------------------------------------------------------

/// Structural assertion catalog used by the workload suite. No absolute
/// millisecond budgets: every rule is a count ceiling, a complexity bound,
/// a memory bound in structural units, or a semantic comparison.
///
/// These fail through Catch2's own FAIL() rather than std::abort(). That is a
/// deliberate choice: abort() skips every local destructor, so a violated gate
/// would discard the records already written, skip the temporary-directory
/// cleanup and leave the RSS poller unjoined — the suite would report a hard
/// crash instead of a named failing assertion, and every workload after it in
/// the same binary would lose its record too.
namespace rules
{

inline void checkCount( long long observed, long long ceiling, const char *what )
{
    if ( observed > ceiling )
        FAIL( "OBS-RULE: " << what << " = " << observed << " > ceiling " << ceiling );
}

inline void checkComplexity( double exponent, double ceiling, const char *what )
{
    if ( exponent > ceiling )
        FAIL( "OBS-RULE: " << what << " exponent " << exponent << " > ceiling " << ceiling );
}

inline void checkMemoryMb( double observedMb, double ceilingMb, const char *what )
{
    if ( observedMb > ceilingMb )
        FAIL( "OBS-RULE: " << what << " " << observedMb << " MB > ceiling " << ceilingMb
                           << " MB" );
}

} // namespace rules

} // namespace sicnu::testing::perf

#endif // SICNU_TESTS_PERF_PERF_OBSERVATORY_H
