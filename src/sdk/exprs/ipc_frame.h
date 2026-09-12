/***************************************************************************
 * exprs/ipc_frame.h — bounded length-prefixed framing for the host-process
 * plugin channel (protocol transport layer)
 *
 * Frame format: [u32 little-endian payload length][payload bytes].
 * The payload is UTF-8 JSON carrying an Ipc envelope (exprs/ipc_envelope.h).
 *
 * Every frame boundary is checked against IpcFrameLimits::maxFrameBytes on
 * BOTH the write and the read side; exceeding it is a protocol violation
 * (E6003) and the channel must be torn down — a peer that cannot respect
 * the negotiated bound is not trusted.
 *
 * The stream abstraction exists so the same framing code runs over
 *   - inherited OS handles in the plugin host worker (Windows HANDLEs /
 *     POSIX fds, see exprs/ipc_stream.h), and
 *   - in-memory pipe pairs in unit tests,
 * with no platform #ifdef in the framing logic itself.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <json/json.h>

namespace exprs {

struct IpcFrameLimits
{
    /// Hard ceiling for one frame payload in bytes (default 32 MiB). Enforced
    /// symmetrically by writer and reader.
    uint32_t maxFrameBytes = 32u * 1024u * 1024u;
};

/// Bidirectional byte stream. Implementations must tolerate one concurrent
/// reader and one concurrent writer (reads happen on the channel reader
/// thread, writes from caller threads behind the channel write mutex).
class IIpcStream
{
public:
    virtual ~IIpcStream() = default;

    /// Reads up to @p cap bytes. Returns bytes read (> 0), 0 when nothing
    /// arrived before @p timeoutMs elapsed, or -1 on EOF/error (details via
    /// lastError()).
    virtual int readSome( char *data, size_t cap, int timeoutMs ) = 0;

    /// Writes exactly @p len bytes; returns false on failure.
    virtual bool writeAll( const char *data, size_t len, std::string &error ) = 0;

    /// Closes both directions. Idempotent; called from any thread.
    virtual void close() = 0;

    /// Description of the last failure ("" when none).
    virtual std::string lastError() const = 0;
};

/// Frame codec. All functions are thread-safe provided the underlying stream
/// has a single writer per call (the channel serializes writes).
namespace IpcFrame {

/// Serializes @p json into one frame and writes it. Fails (without writing
/// anything) when the encoded size exceeds @p limits.maxFrameBytes.
bool writeJson( IIpcStream &stream, const Json::Value &json,
                const IpcFrameLimits &limits, std::string &error );

enum class ReadStatus
{
    Ok,        ///< @p payload carries one complete frame
    Timeout,   ///< @p timeoutMs elapsed before a complete frame arrived
    Eof,       ///< peer closed the stream (clean shutdown candidate)
    TooLarge,  ///< peer announced a frame beyond the cap (E6003; kill channel)
    Error,     ///< stream failure (details in @p error)
};

/// Reads one complete frame. The @p timeoutMs budget applies to one attempt
/// at the WHOLE frame (length prefix + payload), not per read syscall chunk.
/// When @p pendingFrame is non-null, bytes already consumed by a timed-out
/// attempt are kept there (not dropped) and the NEXT read() call resumes the
/// same frame where it stopped: a slow-but-valid frame completes instead of
/// its payload being re-read as a fresh length prefix (framing desync —
/// spurious E6003). Callers that pass no @p pendingFrame keep the legacy
/// one-shot behavior. @p payload is only written on Ok.
ReadStatus read( IIpcStream &stream, std::string &payload,
                 const IpcFrameLimits &limits, int timeoutMs, std::string &error,
                 std::string *pendingFrame = nullptr );

/// Frame length prefix size in bytes (exposed for tests/diagnostics).
constexpr size_t kPrefixBytes = 4;

} // namespace IpcFrame

} // namespace exprs
