// worker_protocol.h — LocalWorkerHost wire protocol v1 (Data Plane 3.0,
// Phase K). Line-delimited UTF-8 JSON over the worker process's stdin/stdout;
// stderr carries diagnostics only.
//
//   host → worker:  {"v":1,"op":"run","jobId":"...","algorithmId":"...","params":{...}}
//                   {"v":1,"op":"cancel","jobId":"..."}
//                   {"v":1,"op":"shutdown"}
//   worker → host:  {"v":1,"op":"ready"}
//                   {"v":1,"op":"progress","jobId":"...","value":0.4,"message":"..."}
//                   {"v":1,"op":"result","jobId":"...","payload":{...}}
//                   {"v":1,"op":"error","jobId":"...","message":"..."}
//
// Rules:
//   - "v" is the protocol MAJOR version; a mismatch is refused before any
//     job runs (forward compatibility for future remote workers).
//   - one "run" per jobId; a cancel for an unknown jobId is answered with an
//     error frame, not ignored.
//   - EOF on stdin means shutdown; the worker finishes the current job best-
//     effort and exits. A SIGKILLed worker surfaces on the host as a crashed
//     invocation — never as a host crash (worker crash != GUI crash).
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::runtime::worker
{

constexpr const char *kWorkerProtocolVersion = "1";

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

/// Parses one protocol frame; returns false on malformed JSON or a version
/// other than 1 (protocol mismatch is a hard refusal, not a best-effort read).
inline bool parseFrame( const std::string &line, Json::Value &frame )
{
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( line.data(), line.data() + line.size(), &frame, &errors ) )
        return false;
    return frame.isMember( "v" ) && frame["v"].asInt() == 1 && frame.isMember( "op" );
}

} // namespace sicnu::runtime::worker
