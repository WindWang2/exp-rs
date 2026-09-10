/***************************************************************************
 * exprs/ipc_stream.cpp
 ***************************************************************************/
#include "exprs/ipc_stream.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#endif

namespace exprs {

// ---------------------------------------------------------------------------
// Memory pipe pair (tests)
// ---------------------------------------------------------------------------

namespace {

class IpcMemoryPipe
{
public:
    void write( const char *data, size_t len )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mBuffer.insert( mBuffer.end(), data, data + len );
        mCv.notify_all();
    }

    /// Returns >0 bytes read, 0 on timeout, -1 when closed and drained.
    int read( char *data, size_t cap, int timeoutMs )
    {
        std::unique_lock<std::mutex> lock( mMutex );
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        while ( mBuffer.empty() )
        {
            if ( mClosed )
                return -1;
            if ( mCv.wait_until( lock, deadline ) == std::cv_status::timeout )
            {
                if ( mBuffer.empty() )
                    return mClosed ? -1 : 0;
            }
        }
        const size_t n = std::min( cap, mBuffer.size() );
        std::copy( mBuffer.begin(), mBuffer.begin() + static_cast<long>( n ), data );
        mBuffer.erase( mBuffer.begin(), mBuffer.begin() + static_cast<long>( n ) );
        return static_cast<int>( n );
    }

    void closePipe()
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mClosed = true;
        mCv.notify_all();
    }

private:
    std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<char> mBuffer;
    bool mClosed = false;
};

class IpcMemoryStream : public IIpcStream
{
public:
    IpcMemoryStream( std::shared_ptr<IpcMemoryPipe> incoming, std::shared_ptr<IpcMemoryPipe> outgoing )
        : mIncoming( std::move( incoming ) ), mOutgoing( std::move( outgoing ) ) {}

    int readSome( char *data, size_t cap, int timeoutMs ) override
    {
        return mIncoming->read( data, cap, timeoutMs );
    }

    bool writeAll( const char *data, size_t len, std::string &error ) override
    {
        if ( mClosed )
        {
            error = "memory stream closed";
            return false;
        }
        mOutgoing->write( data, len );
        return true;
    }

    void close() override
    {
        if ( mClosed )
            return;
        mClosed = true;
        mOutgoing->closePipe();
        mIncoming->closePipe();
    }

    std::string lastError() const override { return mClosed ? "memory stream closed" : std::string(); }

private:
    std::shared_ptr<IpcMemoryPipe> mIncoming;
    std::shared_ptr<IpcMemoryPipe> mOutgoing;
    bool mClosed = false;
};

#ifdef _WIN32
class IpcHandleStreamWindows : public IIpcStream
{
public:
    IpcHandleStreamWindows( HANDLE readHandle, HANDLE writeHandle )
        : mRead( readHandle ), mWrite( writeHandle ) {}
    ~IpcHandleStreamWindows() override { close(); }

    int readSome( char *data, size_t cap, int timeoutMs ) override
    {
        if ( mClosed || mRead == INVALID_HANDLE_VALUE )
            return -1;
        // Peek+sleep poll: keeps the deadline semantics identical to the
        // POSIX poll() path and avoids overlapped-I/O complexity.
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        for ( ;; )
        {
            DWORD available = 0;
            if ( !PeekNamedPipe( mRead, nullptr, 0, nullptr, &available, nullptr ) )
            {
                mError = broken( "PeekNamedPipe" );
                return -1;
            }
            if ( available > 0 )
            {
                DWORD chunk = available;
                if ( chunk > static_cast<DWORD>( cap ) )
                    chunk = static_cast<DWORD>( cap );
                DWORD got = 0;
                if ( !ReadFile( mRead, data, chunk, &got, nullptr ) )
                {
                    mError = broken( "ReadFile" );
                    return -1;
                }
                return got > 0 ? static_cast<int>( got ) : -1;
            }
            if ( std::chrono::steady_clock::now() >= deadline || mClosed )
                return 0;
            Sleep( 5 );
        }
    }

    bool writeAll( const char *data, size_t len, std::string &error ) override
    {
        if ( mClosed || mWrite == INVALID_HANDLE_VALUE )
        {
            error = "handle stream closed";
            return false;
        }
        size_t written = 0;
        while ( written < len )
        {
            DWORD chunk = static_cast<DWORD>( len - written );
            DWORD wrote = 0;
            if ( !WriteFile( mWrite, data + written, chunk, &wrote, nullptr ) )
            {
                error = broken( "WriteFile" );
                return false;
            }
            if ( wrote == 0 )
            {
                error = "WriteFile wrote 0 bytes";
                return false;
            }
            written += wrote;
        }
        return true;
    }

    void close() override
    {
        // Break the pipe from the write side so a blocked peer read fails;
        // handles themselves are owned by the session (not closed here).
        mClosed = true;
        if ( mWrite != INVALID_HANDLE_VALUE )
            CancelIoEx( mWrite, nullptr );
    }

    std::string lastError() const override { return mError; }

private:
    std::string broken( const char *what )
    {
        const DWORD code = GetLastError();
        if ( code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA )
            return std::string( what ) + ": pipe broken (peer exited)";
        return std::string( what ) + " failed with error " + std::to_string( code );
    }

    HANDLE mRead = INVALID_HANDLE_VALUE;
    HANDLE mWrite = INVALID_HANDLE_VALUE;
    std::atomic<bool> mClosed{ false };
    std::string mError;
};
#else
class IpcHandleStreamPosix : public IIpcStream
{
public:
    IpcHandleStreamPosix( int readFd, int writeFd ) : mRead( readFd ), mWrite( writeFd ) {}
    ~IpcHandleStreamPosix() override { close(); }

    int readSome( char *data, size_t cap, int timeoutMs ) override
    {
        if ( mClosed )
            return -1;
        pollfd pfd { mRead, POLLIN, 0 };
        const int ready = ::poll( &pfd, 1, timeoutMs );
        if ( ready == 0 )
            return 0;
        if ( ready < 0 )
        {
            if ( errno == EINTR )
                return 0;
            mError = std::string( "poll failed: " ) + std::strerror( errno );
            return -1;
        }
        const ssize_t n = ::read( mRead, data, cap );
        if ( n > 0 )
            return static_cast<int>( n );
        if ( n == 0 )
        {
            mError = "read: end of stream";
            return -1;
        }
        if ( errno == EAGAIN || errno == EWOULDBLOCK )
            return 0;
        mError = std::string( "read failed: " ) + std::strerror( errno );
        return -1;
    }

    bool writeAll( const char *data, size_t len, std::string &error ) override
    {
        if ( mClosed )
        {
            error = "handle stream closed";
            return false;
        }
        size_t written = 0;
        while ( written < len )
        {
            const ssize_t n = ::write( mWrite, data + written, len - written );
            if ( n > 0 )
            {
                written += static_cast<size_t>( n );
                continue;
            }
            if ( n < 0 && errno == EINTR )
                continue;
            if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) )
            {
                pollfd pfd { mWrite, POLLOUT, 0 };
                ::poll( &pfd, 1, 100 );
                continue;
            }
            error = n == 0 ? "write wrote 0 bytes"
                           : std::string( "write failed: " ) + std::strerror( errno );
            return false;
        }
        return true;
    }

    void close() override { mClosed = true; }
    std::string lastError() const override { return mError; }

private:
    int mRead = -1;
    int mWrite = -1;
    std::atomic<bool> mClosed{ false };
    std::string mError;
};
#endif

} // namespace

void makeIpcMemoryPipePair( std::unique_ptr<IIpcStream> &a, std::unique_ptr<IIpcStream> &b )
{
    auto pipeAtoB = std::make_shared<IpcMemoryPipe>();
    auto pipeBtoA = std::make_shared<IpcMemoryPipe>();
    a = std::make_unique<IpcMemoryStream>( pipeAtoB, pipeBtoA ); // reads A->B pipe
    b = std::make_unique<IpcMemoryStream>( pipeBtoA, pipeAtoB );
}

std::unique_ptr<IIpcStream> makeIpcHandleStream( void *readHandle, void *writeHandle )
{
#ifdef _WIN32
    return std::make_unique<IpcHandleStreamWindows>(
        static_cast<HANDLE>( readHandle ), static_cast<HANDLE>( writeHandle ) );
#else
    return std::make_unique<IpcHandleStreamPosix>(
        static_cast<int>( reinterpret_cast<intptr_t>( readHandle ) ),
        static_cast<int>( reinterpret_cast<intptr_t>( writeHandle ) ) );
#endif
}

bool ipcHandleStreamHandlesValid( void *readHandle, void *writeHandle )
{
    if ( !readHandle || !writeHandle )
        return false;
#ifdef _WIN32
    return readHandle != INVALID_HANDLE_VALUE && writeHandle != INVALID_HANDLE_VALUE;
#else
    return reinterpret_cast<intptr_t>( readHandle ) >= 0
           && reinterpret_cast<intptr_t>( writeHandle ) >= 0;
#endif
}

} // namespace exprs
