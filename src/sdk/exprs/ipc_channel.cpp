/***************************************************************************
 * exprs/ipc_channel.cpp
 ***************************************************************************/
#include "exprs/ipc_channel.h"

#include <cstdio>
#include <stdexcept>

namespace exprs {

namespace {

/// Monotonic ceiling: never raises @p atomic above its current value.
void lowerAtomicCap( std::atomic<uint32_t> &atomic, uint32_t maxFrameBytes )
{
    uint32_t current = atomic.load();
    while ( maxFrameBytes < current
            && !atomic.compare_exchange_weak( current, maxFrameBytes ) )
    {
    }
}

} // namespace

IpcChannel::IpcChannel( std::unique_ptr<IIpcStream> stream )
    : IpcChannel( std::move( stream ), Options{} )
{
}

IpcChannel::IpcChannel( std::unique_ptr<IIpcStream> stream, Options options )
    : mStream( std::move( stream ) ), mOptions( options ),
      mMaxSendFrameBytes( options.frameLimits.maxFrameBytes ),
      mMaxRecvFrameBytes( options.frameLimits.maxFrameBytes )
{
    mReader = std::thread( [this] { readerLoop(); } );
}

IpcChannel::~IpcChannel()
{
    close();
}

std::string IpcChannel::Outcome::statusCode() const
{
    switch ( status )
    {
    case Status::Ok:
        return "";
    case Status::Error:
        return error.code;
    case Status::Timeout:
        return "E6004";
    case Status::Cancelled:
        return "E6009";
    case Status::ChannelClosed:
        return "E6005";
    case Status::ProtocolError:
        // Typed protocol failures: the frame-cap violation (E6003) is
        // distinct from a malformed envelope (E6002); report the real code
        // instead of collapsing both into E6002.
        return error.code == "E6003" ? "E6003" : "E6002";
    }
    return "E6002";
}

void IpcChannel::close()
{
    {
        std::lock_guard<std::mutex> lock( mMutex );
        closeLocked();
    }
    // Reap the reader outside the state lock: it may be waiting for mMutex
    // on its way out, and joining under the lock would deadlock. The reader
    // never calls close() itself, so this cannot self-join.
    if ( mReader.joinable() && mReader.get_id() != std::this_thread::get_id() )
        mReader.join();
}

void IpcChannel::closeLocked()
{
    if ( mClosed )
        return;
    mClosed = true;
    if ( mStream )
        mStream->close();
    mResponseCv.notify_all();
    mRequestCv.notify_all();
}

bool IpcChannel::isOpen() const
{
    return !mClosed;
}

std::string IpcChannel::protocolFailure() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mProtocolFailure;
}

void IpcChannel::setPeerProtocol( int major, int minor )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mPeerMajor = major;
    mPeerMinor = minor;
}

void IpcChannel::lowerFrameCap( uint32_t maxFrameBytes )
{
    lowerAtomicCap( mMaxSendFrameBytes, maxFrameBytes );
    lowerAtomicCap( mMaxRecvFrameBytes, maxFrameBytes );
}

void IpcChannel::setDirectionalFrameCaps( uint32_t maxSendBytes, uint32_t maxRecvBytes )
{
    // 0 means "leave this direction unchanged" (it still carries whatever
    // bound it already had — the shared default or a previously lowered
    // cap). A literal cap of 0 would block every frame, which no
    // negotiation ever asks for.
    if ( maxSendBytes > 0 )
        lowerAtomicCap( mMaxSendFrameBytes, maxSendBytes );
    if ( maxRecvBytes > 0 )
        lowerAtomicCap( mMaxRecvFrameBytes, maxRecvBytes );
}

bool IpcChannel::sendEnvelope( const Ipc::Envelope &envelope, std::string &error,
                               std::string *failureCode )
{
    if ( mClosed )
    {
        error = "channel closed";
        if ( failureCode )
            *failureCode = "E6005";
        return false;
    }
    const Json::Value json = Ipc::encodeEnvelope( envelope );
    std::lock_guard<std::mutex> lock( mWriteMutex );
    IpcFrameLimits limits;
    limits.maxFrameBytes = mMaxSendFrameBytes.load();
    if ( !IpcFrame::writeJson( *mStream, json, limits, error ) )
    {
        // A write failure means the peer is gone or the frame is too large
        // for the cap: both end the channel. The typed code comes from the
        // frame writer (E6003 for a cap violation) so a caller that refused
        // to SEND an oversized frame reports E6003, not the generic E6002.
        if ( failureCode )
            *failureCode = error.find( "E6003" ) != std::string::npos ? "E6003" : "E6005";
        std::lock_guard<std::mutex> stateLock( mMutex );
        if ( mProtocolFailure.empty() )
            mProtocolFailure = error;
        closeLocked();
        return false;
    }
    if ( failureCode )
        *failureCode = "";
    return true;
}

IpcChannel::Outcome IpcChannel::request( const std::string &method, const Json::Value &params,
                                         int deadlineMs, const CancelPredicate &cancelPredicate,
                                         const ProgressSink &progressSink )
{
    Outcome outcome;
    if ( mClosed )
    {
        outcome.status = Outcome::Status::ChannelClosed;
        outcome.error.code = "E6005";
        outcome.error.message = "channel closed before the request was sent";
        return outcome;
    }

    long long id;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        id = mNextId++;
        Pending &pending = mPending[ id ];
        pending.progress = progressSink;
    }

    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Request;
    envelope.id = id;
    envelope.method = method;
    envelope.params = params;
    envelope.deadlineMs = deadlineMs;

    std::string writeError;
    std::string writeCode;
    if ( !sendEnvelope( envelope, writeError, &writeCode ) )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mPending.erase( id );
        outcome.status = Outcome::Status::ProtocolError;
        outcome.error.code = writeCode == "E6003" ? "E6003" : "E6002";
        outcome.error.message = writeError;
        return outcome;
    }

    // Well-behaved plugins answer fast, but the deadline is enforced
    // locally regardless: a hung worker must fail typed, not forever.
    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds( deadlineMs > 0 ? deadlineMs : 60000 );
    bool cancelled = false;
    bool timedOut = false;

    {
        std::unique_lock<std::mutex> lock( mMutex );
        for ( ;; )
        {
            auto it = mPending.find( id );
            if ( it == mPending.end() )
            {
                // Nobody will answer this id anymore (close/protocol fail).
                outcome.status = Outcome::Status::ChannelClosed;
                outcome.error.code = "E6005";
                outcome.error.message = "channel closed while the request was in flight";
                break;
            }
            if ( it->second.done )
            {
                outcome = it->second.outcome;
                mPending.erase( it );
                break;
            }
            if ( cancelPredicate && cancelPredicate() )
            {
                cancelled = true;
                break;
            }
            if ( Clock::now() >= deadline )
            {
                mPending.erase( it );
                outcome.status = Outcome::Status::Timeout;
                outcome.error.code = "E6004";
                outcome.error.message = "request deadline of " + std::to_string( deadlineMs )
                                        + " ms passed without a response";
                timedOut = true;
                break;
            }
            const auto slice = std::chrono::milliseconds( mOptions.cancelPollMs );
            mResponseCv.wait_for( lock, slice );
        }
    }

    if ( timedOut )
    {
        // Protocol 1.1: the deadline only fails the LOCAL wait, but the
        // peer must still be told to stop work on THIS id (per-id cancel;
        // peers in flight are untouched). Kill escalation stays at the
        // session layer, which owns the process.
        cancel( id );
    }

    if ( cancelled )
    {
        // Tell the peer to stop, then give it one short window to finish
        // anyway (a completed execution is a better answer than a typed
        // cancellation). Whatever survives this window is discarded and the
        // caller gets Cancelled; kill escalation is the session's job.
        cancel( id );
        std::unique_lock<std::mutex> lock( mMutex );
        const bool finished = mResponseCv.wait_for(
            lock, std::chrono::milliseconds( mOptions.cancelPollMs ), [&] {
                auto it2 = mPending.find( id );
                return mClosed || ( it2 != mPending.end() && it2->second.done );
            } );
        auto it2 = mPending.find( id );
        if ( finished && it2 != mPending.end() && it2->second.done )
        {
            outcome = it2->second.outcome;
            mPending.erase( it2 );
        }
        else
        {
            if ( it2 != mPending.end() )
                mPending.erase( it2 );
            if ( mClosed )
            {
                outcome.status = Outcome::Status::ChannelClosed;
                outcome.error.code = "E6005";
                outcome.error.message = "channel closed while the request was in flight";
            }
            else
            {
                outcome.status = Outcome::Status::Cancelled;
                outcome.error.code = "E6009";
                outcome.error.message = "request cancelled by the caller";
            }
        }
    }
    return outcome;
}

void IpcChannel::cancel( long long id )
{
    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Cancel;
    envelope.id = id;
    std::string error;
    sendEnvelope( envelope, error );
}

void IpcChannel::cancelAll()
{
    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Cancel;
    envelope.id = -1;
    std::string error;
    sendEnvelope( envelope, error );
}

bool IpcChannel::nextRequest( Ipc::Envelope &request, int timeoutMs )
{
    std::unique_lock<std::mutex> lock( mMutex );
    if ( mIncomingRequests.empty() && !mClosed )
    {
        mRequestCv.wait_for( lock, std::chrono::milliseconds( timeoutMs ) );
    }
    if ( !mIncomingRequests.empty() )
    {
        request = std::move( mIncomingRequests.front() );
        mIncomingRequests.pop_front();
        return true;
    }
    return false;
}

bool IpcChannel::sendResponse( long long id, const Json::Value &result, std::string &error )
{
    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Response;
    envelope.id = id;
    envelope.ok = true;
    envelope.result = result;
    return sendEnvelope( envelope, error );
}

bool IpcChannel::sendError( long long id, const IpcError &error )
{
    Ipc::Envelope envelope = Ipc::makeErrorResponse( id, error.code, error.message,
                                                     error.retryable, error.data );
    std::string writeError;
    return sendEnvelope( envelope, writeError );
}

bool IpcChannel::sendProgress( long long id, double value, const std::string &message )
{
    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Progress;
    envelope.id = id;
    envelope.progress = value;
    envelope.message = message;
    std::string error;
    return sendEnvelope( envelope, error );
}

bool IpcChannel::sendEvent( const std::string &event, const Json::Value &params )
{
    Ipc::Envelope envelope;
    envelope.type = Ipc::MessageType::Event;
    envelope.method = event;
    envelope.params = params;
    std::string error;
    return sendEnvelope( envelope, error );
}

void IpcChannel::setEventSink( std::function<void( const Ipc::Envelope & )> sink )
{
    // Events arriving before a sink is installed are QUEUED and flushed
    // here: the worker.hello handshake races the launcher's registration
    // (the reader thread starts in the channel constructor).
    std::vector<Ipc::Envelope> pending;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mEventSink = sink;
        pending.swap( mPendingEvents );
    }
    for ( const Ipc::Envelope &event : pending )
    {
        if ( sink )
            sink( event );
    }
}

void IpcChannel::setCancelSink( std::function<void( long long )> sink )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mCancelSink = std::move( sink );
}

void IpcChannel::failAllPending( Outcome::Status status, const std::string &code,
                                 const std::string &message )
{
    std::lock_guard<std::mutex> lock( mMutex );
    for ( auto &[ id, pending ] : mPending )
    {
        if ( !pending.done )
        {
            pending.done = true;
            pending.outcome.status = status;
            pending.outcome.error.code = code;
            pending.outcome.error.message = message;
        }
    }
    mResponseCv.notify_all();
    mRequestCv.notify_all();
}

void IpcChannel::handleFrame( const std::string &payload )
{
    Ipc::Envelope envelope;
    std::string error;
    if ( !Ipc::decodeEnvelopePayload( payload, envelope, error ) )
    {
        {
            std::lock_guard<std::mutex> lock( mMutex );
            if ( mProtocolFailure.empty() )
                mProtocolFailure = error;
            closeLocked();
        }
        failAllPending( Outcome::Status::ProtocolError, "E6002", error );
        return;
    }

    switch ( envelope.type )
    {
    case Ipc::MessageType::Response:
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto it = mPending.find( envelope.id );
        if ( it == mPending.end() )
            return; // late answer to an already-failed request: ignore
        Pending &pending = it->second;
        pending.done = true;
        if ( envelope.ok )
        {
            pending.outcome.status = Outcome::Status::Ok;
            pending.outcome.result = envelope.result;
        }
        else
        {
            pending.outcome.status = Outcome::Status::Error;
            pending.outcome.error = envelope.error;
        }
        mResponseCv.notify_all();
        break;
    }
    case Ipc::MessageType::Progress:
    {
        ProgressSink sink;
        {
            std::lock_guard<std::mutex> lock( mMutex );
            auto it = mPending.find( envelope.id );
            if ( it == mPending.end() )
                return;
            sink = it->second.progress;
        }
        if ( sink )
            sink( envelope.progress, envelope.message );
        break;
    }
    case Ipc::MessageType::Request:
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mIncomingRequests.push_back( std::move( envelope ) );
        mRequestCv.notify_all();
        break;
    }
    case Ipc::MessageType::Cancel:
    {
        // Worker side: surface the cancel to the dispatch loop, which owns
        // the cooperative-cancel wiring for its in-flight executions.
        std::function<void( long long )> sink;
        {
            std::lock_guard<std::mutex> lock( mMutex );
            sink = mCancelSink;
        }
        if ( sink )
            sink( envelope.id );
        break;
    }
    case Ipc::MessageType::Event:
    {
        std::function<void( const Ipc::Envelope & )> sink;
        {
            std::lock_guard<std::mutex> lock( mMutex );
            sink = mEventSink;
            if ( !sink )
            {
                // Bounded queue (protocol 1.1): a peer that floods events
                // before a sink is installed cannot exhaust host memory.
                // OLDEST events survive (worker.hello is queued first), the
                // newest are dropped and counted.
                if ( mPendingEvents.size() >= mOptions.maxQueuedEvents )
                {
                    mDroppedEvents.fetch_add( 1 );
                    return;
                }
                mPendingEvents.push_back( std::move( envelope ) );
                return;
            }
        }
        if ( sink )
            sink( envelope );
        break;
    }
    }
}

void IpcChannel::readerLoop()
{
    for ( ;; )
    {
        if ( mClosed )
            break;
        std::string payload;
        std::string error;
        IpcFrameLimits limits;
        limits.maxFrameBytes = mMaxRecvFrameBytes.load();
        const IpcFrame::ReadStatus status =
            IpcFrame::read( *mStream, payload, limits, 200, error );
        if ( status == IpcFrame::ReadStatus::Timeout )
            continue;
        if ( status == IpcFrame::ReadStatus::Eof )
        {
            failAllPending( Outcome::Status::ChannelClosed, "E6005",
                            "peer closed the channel" );
            std::lock_guard<std::mutex> lock( mMutex );
            closeLocked();
            break;
        }
        if ( status == IpcFrame::ReadStatus::Error )
        {
            failAllPending( Outcome::Status::ChannelClosed, "E6005",
                            error.empty() ? "stream failure" : error );
            std::lock_guard<std::mutex> lock( mMutex );
            closeLocked();
            break;
        }
        if ( status == IpcFrame::ReadStatus::TooLarge )
        {
            // Hexdump the stream head: a bogus length means framing desynced
            // or a peer wrote unframed bytes — the dump says which.
            std::string head;
            const size_t dumpLen = payload.size() < 32 ? payload.size() : 32;
            for ( size_t i = 0; i < dumpLen; ++i )
            {
                char byte[ 4 ];
                std::snprintf( byte, sizeof( byte ), "%02x ",
                               static_cast<unsigned char>( payload[ i ] ) );
                head += byte;
            }
            std::string detailed = error + "; stream head: " + head;
            {
                std::lock_guard<std::mutex> lock( mMutex );
                if ( mProtocolFailure.empty() )
                    mProtocolFailure = detailed;
                closeLocked();
            }
            failAllPending( Outcome::Status::ProtocolError, "E6003", detailed );
            break;
        }
        try
        {
            handleFrame( payload );
        }
        catch ( const std::exception &exception )
        {
            // A malformed peer frame must never kill the reader thread
            // (uncaught -> terminate -> host dies). Record, close, fail
            // pending typed - the protocol-error contract.
            std::string failure = std::string( "reader exception: " ) + exception.what();
            {
                std::lock_guard<std::mutex> lock( mMutex );
                if ( mProtocolFailure.empty() )
                    mProtocolFailure = failure;
                closeLocked();
            }
            failAllPending( Outcome::Status::ProtocolError, "E6002", failure );
            break;
        }
        if ( mClosed )
            break;
    }
}

} // namespace exprs
