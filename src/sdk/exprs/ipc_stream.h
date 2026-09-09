/***************************************************************************
 * exprs/ipc_stream.h — IIpcStream implementations
 *
 *   - IpcMemoryPipePair: in-memory connected pair (unit tests, loopback).
 *   - makeIpcHandleStream: OS-handle transport used by the plugin host
 *     worker (Windows: HANDLEs; POSIX: fds). Reads poll with a deadline so
 *     channel code never needs OS-specific waits.
 *
 * The platform transport is deliberately dumb (blocking byte pipes): all
 * framing, deadlines and validation live above it, so tests exercise the
 * same logic the production worker uses.
 ***************************************************************************/
#pragma once

#include <memory>

#include "exprs/ipc_frame.h"

namespace exprs {

/// Connected in-memory stream pair. Unbounded intermediate buffering is fine
/// for tests; production traffic is bounded by the frame cap.
void makeIpcMemoryPipePair( std::unique_ptr<IIpcStream> &a, std::unique_ptr<IIpcStream> &b );

/// Platform stream over two unidirectional OS handles:
///   @p readHandle  — data written by the peer arrives here
///   @p writeHandle — data written here reaches the peer
/// On POSIX these are file descriptors (int); on Windows, HANDLEs
/// (passed as void*). Ownership stays with the caller: the stream uses the
/// handles but never closes them at destruction — close() is explicit.
std::unique_ptr<IIpcStream> makeIpcHandleStream( void *readHandle, void *writeHandle );

/// true when the platform stream was opened on a valid handle pair.
bool ipcHandleStreamHandlesValid( void *readHandle, void *writeHandle );

} // namespace exprs
