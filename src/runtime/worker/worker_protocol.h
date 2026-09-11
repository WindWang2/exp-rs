// worker_protocol.h — LocalWorkerHost wire protocol v1 (Data Plane 3.0,
// Phase K; extended by Execution Plane 7.0 without a major-version bump).
// Line-delimited UTF-8 JSON over the worker process's stdin/stdout;
// stderr carries diagnostics only.
//
//   host → worker:  {"v":1,"op":"run","jobId":"...","algorithmId":"...","params":{...}}
//                   {"v":1,"op":"cancel","jobId":"..."}
//                   {"v":1,"op":"shutdown"}
//   worker → host:  {"v":1,"op":"ready","caps":["progress","cancelAck",...]}   (caps optional)
//                   {"v":1,"op":"progress","jobId":"...","value":0.4,"message":"..."}
//                   {"v":1,"op":"ack","jobId":"...","kind":"cancel"}           (optional op)
//                   {"v":1,"op":"result","jobId":"...","payload":{...}
//                               [,"outputs":[{"path":..,"sizeBytes":..,"lastModifiedMsec":..}]]}
//                   {"v":1,"op":"error","jobId":"...","message":"..."[,"code":"cancelled"]}
//
// Rules:
//   - "v" is the protocol MAJOR version; a mismatch is refused before any
//     job runs (forward compatibility for future remote workers).
//   - one "run" per jobId; a cancel for an unknown jobId is answered with an
//     error frame, not ignored.
//   - EOF on stdin means shutdown; the worker finishes the current job best-
//     effort and exits. A SIGKILLed worker surfaces on the host as a crashed
//     invocation — never as a host crash (worker crash != GUI crash).
//   - Extension rule (7.0): everything new is an OPTIONAL field or an op the
//     peer may not know. A v1 host that does not recognize "ack"/"caps"/
//     "code"/"outputs" ignores the unknown member/op and keeps the old
//     behavior; a v1 worker that does not send them is fully serviceable. A
//     host MUST treat an unknown op as "keep waiting" (bounded by its own
//     deadline), never as a protocol violation.
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::runtime::worker
{

constexpr const char *kWorkerProtocolVersion = "1";

/// Capability tokens a worker may list in its ready frame (all optional; a
/// host must work with any subset). Declared as strings so old binaries that
/// never parse them stay byte-compatible.
inline const char *kWorkerCapProgress = "progress";               ///< worker emits progress frames
inline const char *kWorkerCapCancelAck = "cancelAck";             ///< worker acks cancel requests
inline const char *kWorkerCapStructuredErrors = "structuredErrors"; ///< error frames carry "code"
inline const char *kWorkerCapOutputIdentity = "outputIdentity";   ///< result frames carry "outputs"

/// Frames are SINGLE LINE (newline-delimited transport): every writer must
/// emit compact JSON.
inline std::string compactFrame( const Json::Value &frame )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    return Json::writeString( builder, frame );
}

inline std::string makeRunRequest( const std::string &jobId, const std::string &algorithmId,
                                   const Json::Value &params )
{
    Json::Value request;
    request["v"] = 1;
    request["op"] = "run";
    request["jobId"] = jobId;
    request["algorithmId"] = algorithmId;
    request["params"] = params;
    return compactFrame( request );
}

inline std::string makeCancelRequest( const std::string &jobId )
{
    Json::Value request;
    request["v"] = 1;
    request["op"] = "cancel";
    request["jobId"] = jobId;
    return compactFrame( request );
}

inline std::string makeShutdownRequest()
{
    Json::Value request;
    request["v"] = 1;
    request["op"] = "shutdown";
    return compactFrame( request );
}

/// Ready handshake with optional capability advertisement (7.0). An empty
/// @p caps list serializes exactly the legacy frame.
inline std::string makeReadyFrame( const std::vector<std::string> &caps = {} )
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = "ready";
    if ( !caps.empty() )
    {
        Json::Value capArray( Json::arrayValue );
        for ( const auto &cap : caps )
            capArray.append( cap );
        frame["caps"] = capArray;
    }
    return compactFrame( frame );
}

/// Worker → host acknowledgement (7.0, optional op). @p kind is "cancel" for
/// a received cancel request. Hosts that do not know "ack" ignore it.
inline std::string makeAckFrame( const std::string &jobId, const std::string &kind )
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = "ack";
    frame["jobId"] = jobId;
    frame["kind"] = kind;
    return compactFrame( frame );
}

inline std::string makeResultFrame( const std::string &jobId, const Json::Value &payload,
                                    const Json::Value &outputs = Json::Value() )
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = "result";
    frame["jobId"] = jobId;
    frame["payload"] = payload;
    if ( outputs.isArray() && !outputs.empty() )
        frame["outputs"] = outputs; // optional output identity manifest (7.0)
    return compactFrame( frame );
}

inline std::string makeErrorFrame( const std::string &jobId, const std::string &message,
                                   const std::string &code = {} )
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = "error";
    frame["jobId"] = jobId;
    frame["message"] = message;
    if ( !code.empty() )
        frame["code"] = code; // structured error class (7.0, optional)
    return compactFrame( frame );
}

/// Parses one protocol frame; returns false on malformed JSON or a version
/// other than 1 (protocol mismatch is a hard refusal, not a best-effort read).
/// An unknown "op" still parses true — the CALLER decides whether it knows the
/// op (the extension rule above); this keeps new optional ops compatible.
inline bool parseFrame( const std::string &line, Json::Value &frame )
{
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( line.data(), line.data() + line.size(), &frame, &errors ) )
        return false;
    // isIntegral BEFORE asInt: asInt() throws/aborts on non-numeric values
    // (e.g. {"v":{}} or {"v":"1"}), so a malformed peer frame must not be
    // able to crash the host or worker at the protocol gate itself.
    // isObject FIRST: jsoncpp's isMember()/find() throw LogicError on
    // non-object roots (arrays, strings, numbers) — a malformed peer frame
    // must not crash the host or worker at the protocol gate itself.
    return frame.isObject() && frame.isMember( "v" ) && frame["v"].isInt()
           && frame["v"].asInt() == 1 && frame.isMember( "op" );
}

/// True when a ready frame advertises @p cap (frame without "caps" = none).
inline bool frameHasCapability( const Json::Value &readyFrame, const char *cap )
{
    const Json::Value &caps = readyFrame["caps"];
    if ( !caps.isArray() )
        return false;
    for ( const auto &entry : caps )
        if ( entry.isString() && entry.asString() == cap )
            return true;
    return false;
}

/// Structured error class of an error frame ("" when absent = legacy worker).
inline std::string frameErrorCode( const Json::Value &errorFrame )
{
    return errorFrame.isMember( "code" ) && errorFrame["code"].isString()
               ? errorFrame["code"].asString()
               : std::string();
}

/// Output identity manifest of a result frame (null when absent). Each entry
/// is {"path","sizeBytes","lastModifiedMsec"} as produced by the worker.
inline Json::Value frameOutputIdentity( const Json::Value &resultFrame )
{
    return resultFrame.isMember( "outputs" ) ? resultFrame["outputs"] : Json::Value();
}

} // namespace sicnu::runtime::worker
