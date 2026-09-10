/***************************************************************************
 * exprs/host_protocol.h — host-process protocol versioning (public SDK)
 *
 * Fourth versioning axis (docs/sdk/versioning.md): the wire protocol between
 * the ExpRS host and an out-of-process plugin host worker.
 *
 *   - MAJOR mismatch is incompatible: the connection is refused with
 *     E6001 before any plugin code is loaded.
 *   - MINOR is additive: a peer with a lower minor is compatible; a peer
 *     with a higher minor is refused (the extra surface is unknown).
 *
 * Declared as macros like the rest of exprs/version.h so the installed SDK
 * headers carry the value the binary was built with.
 ***************************************************************************/
#pragma once

#define EXP_RS_HOST_PROTOCOL_VERSION_MAJOR 1
#define EXP_RS_HOST_PROTOCOL_VERSION_MINOR 0

#define EXP_RS_STRINGIFY_HOST_( x ) #x
#define EXP_RS_STRINGIFY_HOST( x ) EXP_RS_STRINGIFY_HOST_( x )

/// Host protocol version as "MAJOR.MINOR" (e.g. "1.0").
#define EXP_RS_HOST_PROTOCOL_VERSION \
    EXP_RS_STRINGIFY_HOST( EXP_RS_HOST_PROTOCOL_VERSION_MAJOR ) "." \
    EXP_RS_STRINGIFY_HOST( EXP_RS_HOST_PROTOCOL_VERSION_MINOR )

namespace exprs {

/// Host protocol version this SDK speaks.
inline int hostProtocolVersionMajor() { return EXP_RS_HOST_PROTOCOL_VERSION_MAJOR; }
inline int hostProtocolVersionMinor() { return EXP_RS_HOST_PROTOCOL_VERSION_MINOR; }

} // namespace exprs
