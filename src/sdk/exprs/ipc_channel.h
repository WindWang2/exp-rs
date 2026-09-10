/***************************************************************************
 * exprs/ipc_channel.h — request/response channel over the host-process
 * protocol (framing + envelope + correlation, transport-agnostic)
 *
 * One channel per worker connection. A background reader thread owns the
 * stream's read side; writes are serialized behind a mutex so progress
 * events (worker side) and cancels (host side) can interleave with data
 * frames. Both endpoints use this class:
 *
 *   host   : request() correlates an outgoing request with its response,
 *            enforcing the caller's deadline and cancel predicate; a
 *            deadline only fails the local wait — the kill ladder (cancel
 *            frame, grace, terminate) lives in the session layer.
 *   worker : nextRequest() yields incoming requests; sendResponse /
 *            sendError / sendProgress / sendEvent answer them.
 *
 * Failure semantics (typed, never silent):
 *   - deadline             -> Status::Timeout  (E6004)
 *   - cancel predicate     -> Status::Cancelled (E6009)
 *   - peer stream EOF      -> Status::ChannelClosed (E6005 for in-flight)
 *   - frame cap violation  -> Status::ProtocolError (E6003, channel closed)
 *   - malformed envelope   -> Status::ProtocolError (E6002, channel closed)
 * After a protocol failure the channel is closed for good: a peer that
 * corrupts the framing is untrusted, restart policy decides what next.
 ***************************************************************************/
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "exprs/ipc_envelope.h"
#include "exprs/ipc_frame.h"

namespace exprs {

class IpcChannel
{
public:
    struct Options
    {
        IpcFrameLimits frameLimits;
        /// Slice between cancel-predicate polls while waiting for a response.
        int cancelPollMs;
        /// Explicit (not a default member initializer): newer Clang rejects
        /// the enclosing class's `Options options = {}` default argument
        /// when it would ODR-use a default member initializer from within
        /// IpcChannel's own definition.
        Options() : cancelPollMs( 100 ) {}
    };

    explicit IpcChannel( std::unique_ptr<IIpcStream> stream );
    explicit IpcChannel( std::unique_ptr<IIpcStream> stream, Options options );
    ~IpcChannel();

    IpcChannel( const IpcChannel & ) = delete;
    IpcChannel &operator=( const IpcChannel & ) = delete;

    // -- host side -----------------------------------------------------------
    struct Outcome
    {
        enum class Status
        {
            Ok,             ///< @p result carries the peer result
            Error,          ///< peer answered a structured error (@p error)
            Timeout,        ///< deadline passed (E6004)
            Cancelled,      ///< cancel predicate fired (E6009)
            ChannelClosed,  ///< peer gone (E6005 at session level)
            ProtocolError,  ///< framing/envelope violation (E6002/E6003)
        };
        Status status = Status::ChannelClosed;
        Json::Value result;
        IpcError error;
        /// Diagnostic code string for the failure ("", "E6004", ...).
        std::string statusCode() const;
    };

    using ProgressSink = std::function<void( double progress, const std::string &message )>;
    using CancelPredicate = std::function<bool()>;

    /// Sends one request and waits (bounded, cancellable). Safe to call from
    /// any thread; multiple concurrent requests are correlated by id.
    Outcome request( const std::string &method, const Json::Value &params, int deadlineMs,
                     const CancelPredicate &cancelPredicate = {},
                     const ProgressSink &progressSink = {} );

    /// Sends a cancel frame for @p id (host side, e.g. on local timeout).
    void cancel( long long id );
    /// Sends a cancel frame addressed to ALL in-flight requests (id -1;
    /// worker dispatches treat it as broadcast). Kill-ladder companion.
    void cancelAll();

    // -- worker side ---------------------------------------------------------
    /// Pops the next incoming request (blocking up to @p timeoutMs).
    /// Returns false on timeout or closed channel.
    bool nextRequest( Ipc::Envelope &request, int timeoutMs );

    bool sendResponse( long long id, const Json::Value &result, std::string &error );
    bool sendError( long long id, const IpcError &error );
    bool sendProgress( long long id, double value, const std::string &message );
    bool sendEvent( const std::string &event, const Json::Value &params );

    /// Callback invoked (reader thread) for peer events. Set before traffic.
    void setEventSink( std::function<void( const Ipc::Envelope &event )> sink );

    /// Callback invoked (reader thread) when the peer cancels @p id.
    /// Worker side: mark the request cancelled for its execution context.
    void setCancelSink( std::function<void( long long id )> sink );

    // -- state ---------------------------------------------------------------
    /// Closes the stream and stops the reader. In-flight waiters are failed
    /// with ChannelClosed. Idempotent.
    void close();
    bool isOpen() const;
    /// Last protocol-level failure description ("" when none).
    std::string protocolFailure() const;
    /// Negotiated peer protocol minor (from handshake responses; -1 = unset).
    void setPeerProtocol( int major, int minor );
    int peerProtocolMajor() const { return mPeerMajor; }
    int peerProtocolMinor() const { return mPeerMinor; }

private:
    struct Pending
    {
        ProgressSink progress;
        IpcChannel::Outcome outcome;
        bool done = false;
    };

    bool sendEnvelope( const Ipc::Envelope &envelope, std::string &error );
    void readerLoop();
    void handleFrame( const std::string &payload );
    void failAllPending( Outcome::Status status, const std::string &code, const std::string &message );
    void closeLocked();

    std::unique_ptr<IIpcStream> mStream;
    const Options mOptions;

    std::mutex mWriteMutex;             ///< serializes frame writes
    mutable std::mutex mMutex;          ///< state below (minus atomics)
    std::condition_variable mResponseCv;
    std::condition_variable mRequestCv;
    std::map<long long, Pending> mPending;
    std::deque<Ipc::Envelope> mIncomingRequests;
    std::vector<Ipc::Envelope> mPendingEvents;   ///< arrived before a sink existed
    std::function<void( const Ipc::Envelope & )> mEventSink;
    std::function<void( long long )> mCancelSink;
    std::string mProtocolFailure;

    std::thread mReader;
    std::atomic<bool> mClosed{ false };
    std::atomic<long long> mNextId{ 1 };
    int mPeerMajor = -1;
    int mPeerMinor = -1;
};

} // namespace exprs
