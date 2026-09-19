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
/// (passed as void*).
///
/// OWNERSHIP: the stream takes ownership of BOTH handles. close() is
/// idempotent (exactly one caller wins the atomic exchange) and releases
/// them; the destructor closes any handle still owned. The session must
/// therefore hand the parent-side pipe ends to exactly one stream and never
/// close them itself — the single-owner rule that fixes the per-session
/// descriptor leak (#1036). Callers that must keep a handle open (the
/// launcher's copies of the CHILD-side ends) close theirs before construction
/// as before.
///
/// THREADING: close() must not run concurrently with readSome()/writeAll()
/// on the same stream. IpcChannel enforces this ordering (it joins its
/// reader before closing the stream); direct users must do the same.
std::unique_ptr<IIpcStream> makeIpcHandleStream( void *readHandle, void *writeHandle );

/// true when the platform stream was opened on a valid handle pair.
bool ipcHandleStreamHandlesValid( void *readHandle, void *writeHandle );

} // namespace exprs
