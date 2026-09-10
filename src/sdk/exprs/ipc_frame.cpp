/***************************************************************************
 * exprs/ipc_frame.cpp
 ***************************************************************************/
#include "exprs/ipc_frame.h"

#include <chrono>
#include <cstring>

namespace exprs {

namespace IpcFrame {

namespace {

using Clock = std::chrono::steady_clock;

/// Milliseconds remaining until @p deadline (never negative).
int remainingMs( Clock::time_point deadline )
{
    const auto now = Clock::now();
    if ( now >= deadline )
        return 0;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>( deadline - now ).count() );
}

/// Reads exactly @p len bytes within the overall @p deadline. Returns false
/// on timeout/EOF/error with @p timedOut / @p eof flagging which.
bool readExact( IIpcStream &stream, char *data, size_t len, Clock::time_point deadline,
                bool &timedOut, bool &eof, std::string &error )
{
    size_t got = 0;
    while ( got < len )
    {
        const int slice = remainingMs( deadline );
        if ( slice <= 0 )
        {
            timedOut = true;
            return false;
        }
        const int n = stream.readSome( data + got, len - got, slice );
        if ( n > 0 )
        {
            got += static_cast<size_t>( n );
            continue;
        }
        if ( n == 0 )
            continue; // timeout slice; loop re-checks the overall deadline
        eof = true;   // n < 0: EOF or stream error
        error = stream.lastError();
        return false;
    }
    return true;
}

} // namespace

bool writeJson( IIpcStream &stream, const Json::Value &json,
                const IpcFrameLimits &limits, std::string &error )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    builder[ "commentStyle" ] = "None";
    const std::string payload = Json::writeString( builder, json );

    if ( payload.size() > limits.maxFrameBytes )
    {
        error = "frame payload of " + std::to_string( payload.size() )
                + " bytes exceeds the negotiated cap of "
                + std::to_string( limits.maxFrameBytes ) + " bytes (E6003)";
        return false;
    }
    if ( payload.size() > 0xFFFFFFFFull - kPrefixBytes )
    {
        error = "frame payload exceeds the u32 length prefix";
        return false;
    }

    uint8_t prefix[ kPrefixBytes ];
    const uint32_t length = static_cast<uint32_t>( payload.size() );
    prefix[ 0 ] = static_cast<uint8_t>( length & 0xFFu );
    prefix[ 1 ] = static_cast<uint8_t>( ( length >> 8 ) & 0xFFu );
    prefix[ 2 ] = static_cast<uint8_t>( ( length >> 16 ) & 0xFFu );
    prefix[ 3 ] = static_cast<uint8_t>( ( length >> 24 ) & 0xFFu );

    if ( !stream.writeAll( reinterpret_cast<const char *>( prefix ), kPrefixBytes, error ) )
        return false;
    if ( !payload.empty()
         && !stream.writeAll( payload.data(), payload.size(), error ) )
        return false;
    return true;
}

ReadStatus read( IIpcStream &stream, std::string &payload,
                 const IpcFrameLimits &limits, int timeoutMs, std::string &error )
{
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds( timeoutMs );

    uint8_t prefix[ kPrefixBytes ];
    bool timedOut = false;
    bool eof = false;
    if ( !readExact( stream, reinterpret_cast<char *>( prefix ), kPrefixBytes, deadline,
                     timedOut, eof, error ) )
    {
        if ( timedOut )
            return ReadStatus::Timeout;
        return eof ? ReadStatus::Eof : ReadStatus::Error;
    }

    uint32_t length = static_cast<uint32_t>( prefix[ 0 ] )
                      | ( static_cast<uint32_t>( prefix[ 1 ] ) << 8 )
                      | ( static_cast<uint32_t>( prefix[ 2 ] ) << 16 )
                      | ( static_cast<uint32_t>( prefix[ 3 ] ) << 24 );
    if ( length > limits.maxFrameBytes )
    {
        error = "peer announced a frame of " + std::to_string( length )
                + " bytes, beyond the negotiated cap of "
                + std::to_string( limits.maxFrameBytes ) + " bytes (E6003)";
        return ReadStatus::TooLarge;
    }

    payload.assign( length, '\0' );
    if ( length == 0 )
        return ReadStatus::Ok;
    if ( !readExact( stream, payload.data(), length, deadline, timedOut, eof, error ) )
    {
        if ( timedOut )
            return ReadStatus::Timeout;
        return eof ? ReadStatus::Eof : ReadStatus::Error;
    }
    return ReadStatus::Ok;
}

} // namespace IpcFrame

} // namespace exprs
