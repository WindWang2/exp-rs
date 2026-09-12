/***************************************************************************
 * exprs/ipc_envelope.h — the versioned message envelope of the plugin
 * host-process protocol (application layer over exprs/ipc_frame.h)
 *
 * Compatibility rule (mirrors the plugin API axis in exprs/version.h):
 * same protocol MAJOR required; a peer MINOR may be <= the local MINOR
 * (the higher minor is strictly additive). Violations are refused with
 * E6001 BEFORE any plugin code is loaded into the worker.
 *
 * Unknown envelope FIELDS are ignored (additive evolution); unknown message
 * TYPES and unknown request METHODS are answered with typed refusals
 * (decode failure / E6008) so an old peer never guesses.
 *
 * Bounded payloads: large artifacts travel as workspace-contained file
 * references, never as raw bytes through the channel — the frame cap in
 * exprs/ipc_frame.h is the enforcement point.
 ***************************************************************************/
#pragma once

#include <string>

#include <json/json.h>

namespace exprs {

/// Structured error carried in a failed response. @p code is a stable
/// diagnostic code string ("E6xxx" — see exprs/plugin_diagnostics.h).
struct IpcError
{
    std::string code;
    std::string message;
    bool retryable = false;
    Json::Value data;   ///< structured detail (optional)

    Json::Value toJson() const;
    static IpcError fromJson( const Json::Value &json );
};

namespace Ipc {

/// Protocol version implemented by this SDK (kept in lockstep with
/// EXP_RS_HOST_PROTOCOL_VERSION, exprs/host_protocol.h). 1.1 adds the
/// declarative-UI methods and concurrency surface additively; 1.2 adds the
/// hello "features" advertisement and per-direction frame caps.
constexpr int kProtocolVersionMajor = 1;
constexpr int kProtocolVersionMinor = 2;

enum class MessageType
{
    Request,    ///< host -> worker (or reserved for future reverse calls)
    Response,   ///< reply to a Request (correlated by id)
    Progress,   ///< worker -> host progress for an in-flight request
    Cancel,     ///< host -> worker: stop working on id
    Event,      ///< worker -> host unsolicited event (log etc.), id 0
};

const char *messageTypeName( MessageType type );
/// Returns false for unknown type names (decode failure, not a guess).
bool messageTypeFromName( const std::string &name, MessageType &out );

/// One protocol message. Field relevance depends on @p type:
///   Request  : id, method, params, deadlineMs
///   Response : id, ok + (result | error)
///   Progress : id, progress, message
///   Cancel   : id
///   Event    : event name in @p method, payload in @p params
struct Envelope
{
    MessageType type = MessageType::Request;
    long long id = 0;
    std::string method;
    Json::Value params;
    bool ok = true;
    Json::Value result;
    IpcError error;
    double progress = 0.0;
    std::string message;
    int deadlineMs = 0;   ///< request hint; enforcement is host-side
};

/// True when a peer (@p peerMajor/@p peerMinor) can talk to a local
/// (@p localMajor/@p localMinor) endpoint. On false, @p reason explains why.
bool isProtocolCompatible( int localMajor, int localMinor, int peerMajor, int peerMinor,
                           std::string &reason );

/// Encodes one envelope as a JSON object ("v" carries the protocol major).
Json::Value encodeEnvelope( const Envelope &envelope );

/// Parses and validates one envelope JSON object. Unknown extra fields are
/// ignored; a structurally invalid envelope is a decode failure (E6002).
bool decodeEnvelope( const Json::Value &json, Envelope &envelope, std::string &error );

/// Convenience: parses a frame payload string as UTF-8 JSON, then decodes it.
bool decodeEnvelopePayload( const std::string &payload, Envelope &envelope, std::string &error );

/// Builds the standard typed failure envelope for @p id.
Envelope makeErrorResponse( long long id, const std::string &code, const std::string &message,
                            bool retryable = false, Json::Value data = {} );

} // namespace Ipc

} // namespace exprs
