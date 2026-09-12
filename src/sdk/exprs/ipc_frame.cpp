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

/// Reads exactly @p len bytes within the overall @p deadline, updating @p got
/// with the bytes consumed so far. Returns false on timeout/EOF/error with
/// @p timedOut / @p eof flagging which; on return @p got says how much of the
/// frame is already in the buffer, so a timed-out attempt can be resumed
/// instead of restarting from scratch.
bool readExact( IIpcStream &stream, char *data, size_t &got, size_t len,
                Clock::time_point deadline, bool &timedOut, bool &eof, std::string &error )
{
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
                 const IpcFrameLimits &limits, int timeoutMs, std::string &error,
                 std::string *pendingFrame )
{
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds( timeoutMs );

    // Bytes of the CURRENT frame already consumed from the stream by earlier
    // timed-out attempts (length prefix first, then payload). Resuming —
    // never restarting — is what keeps a slow-but-valid frame from
    // desyncing the stream: re-reading mid-frame bytes as a fresh length
    // prefix produces a bogus length and a spurious E6003 teardown.
    std::string localPending;
    std::string &pending = pendingFrame ? *pendingFrame : localPending;
    payload.clear();

    bool timedOut = false;
    bool eof = false;

    // Phase 1: the length prefix.
    size_t got = pending.size();
    if ( got < kPrefixBytes )
    {
        pending.resize( kPrefixBytes );
        if ( !readExact( stream, &pending[ 0 ], got, kPrefixBytes, deadline,
                         timedOut, eof, error ) )
        {
            pending.resize( got ); // keep only what was actually consumed
            if ( timedOut )
                return ReadStatus::Timeout; // resumed by the next read() call
            return eof ? ReadStatus::Eof : ReadStatus::Error;
        }
    }

    // Length bytes are unsigned. Casting a signed char 0x80..0xFF
    // straight to uint32_t sign-extends (0x80 → 4294967168) and a legal
    // 128–255 byte frame is refused as TooLarge (E6003) on MSVC / signed-char
    // toolchains.
    const auto lengthByte = []( char c ) -> uint32_t {
        return static_cast<uint32_t>( static_cast<unsigned char>( c ) );
    };
    uint32_t length = lengthByte( pending[ 0 ] )
                      | ( lengthByte( pending[ 1 ] ) << 8 )
                      | ( lengthByte( pending[ 2 ] ) << 16 )
                      | ( lengthByte( pending[ 3 ] ) << 24 );
    if ( length > limits.maxFrameBytes )
    {
        error = "peer announced a frame of " + std::to_string( length )
                + " bytes, beyond the negotiated cap of "
                + std::to_string( limits.maxFrameBytes ) + " bytes (E6003)";
        pending.clear();
        return ReadStatus::TooLarge;
    }

    // Phase 2: the payload. Bytes from earlier timed-out attempts may
    // already sit behind the prefix in @p pending — read only the rest.
    got = pending.size() - kPrefixBytes;
    pending.resize( kPrefixBytes + length );
    if ( !readExact( stream, &pending[ 0 ] + kPrefixBytes, got, length,
                     deadline, timedOut, eof, error ) )
    {
        // `got` is payload bytes consumed, not the whole pending buffer.
        // Keep the 4-byte length prefix so the next read() resumes the same
        // frame instead of treating leftover payload as a fresh length.
        pending.resize( kPrefixBytes + got );
        if ( timedOut )
            return ReadStatus::Timeout; // resumed by the next read() call
        return eof ? ReadStatus::Eof : ReadStatus::Error;
    }

    payload.assign( pending, kPrefixBytes, length );
    pending.clear();
    return ReadStatus::Ok;
}

} // namespace IpcFrame

} // namespace exprs
