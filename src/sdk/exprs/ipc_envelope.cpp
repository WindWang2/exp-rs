/***************************************************************************
 * exprs/ipc_envelope.cpp
 ***************************************************************************/
#include "exprs/ipc_envelope.h"

namespace exprs {

Json::Value IpcError::toJson() const
{
    Json::Value json( Json::objectValue );
    json["code"] = code;
    json["message"] = message;
    json["retryable"] = retryable;
    if ( !data.isNull() && data.isObject() )
        json["data"] = data;
    return json;
}

IpcError IpcError::fromJson( const Json::Value &json )
{
    IpcError error;
    if ( !json.isObject() )
        return error;
    // Every field is type-checked: this parses UNTRUSTED peer frames, and
    // jsoncpp's asString()/asBool() throw on mismatched types - an
    // exception here would escape onto the reader thread and kill the host.
    const Json::Value &codeValue = json[ "code" ];
    if ( codeValue.isString() )
        error.code = codeValue.asString();
    const Json::Value &messageValue = json[ "message" ];
    if ( messageValue.isString() )
        error.message = messageValue.asString();
    const Json::Value &retryableValue = json[ "retryable" ];
    if ( retryableValue.isBool() )
        error.retryable = retryableValue.asBool();
    const Json::Value &data = json[ "data" ];
    if ( !data.isNull() )
        error.data = data;   // object OR array (e.g. diagnostic log batches)
    return error;
}

namespace Ipc {

const char *messageTypeName( MessageType type )
{
    switch ( type )
    {
    case MessageType::Request:
        return "request";
    case MessageType::Response:
        return "response";
    case MessageType::Progress:
        return "progress";
    case MessageType::Cancel:
        return "cancel";
    case MessageType::Event:
        return "event";
    }
    return "unknown";
}

bool messageTypeFromName( const std::string &name, MessageType &out )
{
    if ( name == "request" )
        out = MessageType::Request;
    else if ( name == "response" )
        out = MessageType::Response;
    else if ( name == "progress" )
        out = MessageType::Progress;
    else if ( name == "cancel" )
        out = MessageType::Cancel;
    else if ( name == "event" )
        out = MessageType::Event;
    else
        return false;
    return true;
}

bool isProtocolCompatible( int localMajor, int localMinor, int peerMajor, int peerMinor,
                           std::string &reason )
{
    if ( peerMajor != localMajor )
    {
        reason = "protocol major mismatch: local " + std::to_string( localMajor )
                 + ", peer " + std::to_string( peerMajor ) + " (E6001)";
        return false;
    }
    if ( peerMinor > localMinor )
    {
        reason = "protocol minor " + std::to_string( peerMinor )
                 + " is newer than the supported minor " + std::to_string( localMinor )
                 + " (E6001)";
        return false;
    }
    return true;
}

Json::Value encodeEnvelope( const Envelope &envelope )
{
    Json::Value json( Json::objectValue );
    json["v"] = kProtocolVersionMajor;
    json["type"] = messageTypeName( envelope.type );
    if ( envelope.type == MessageType::Event )
    {
        json["event"] = envelope.method;
        if ( !envelope.params.isNull() )
            json["params"] = envelope.params;
    }
    else if ( envelope.type == MessageType::Request )
    {
        json["id"] = static_cast<Json::Int64>( envelope.id );
        json["method"] = envelope.method;
        if ( !envelope.params.isNull() )
            json["params"] = envelope.params;
        if ( envelope.deadlineMs > 0 )
            json["deadlineMs"] = envelope.deadlineMs;
    }
    else if ( envelope.type == MessageType::Response )
    {
        json["id"] = static_cast<Json::Int64>( envelope.id );
        json["ok"] = envelope.ok;
        if ( envelope.ok )
        {
            if ( !envelope.result.isNull() )
                json["result"] = envelope.result;
        }
        else
        {
            json["error"] = envelope.error.toJson();
        }
    }
    else if ( envelope.type == MessageType::Progress )
    {
        json["id"] = static_cast<Json::Int64>( envelope.id );
        json["value"] = envelope.progress;
        if ( !envelope.message.empty() )
            json["message"] = envelope.message;
    }
    else if ( envelope.type == MessageType::Cancel )
    {
        json["id"] = static_cast<Json::Int64>( envelope.id );
    }
    return json;
}

bool decodeEnvelope( const Json::Value &json, Envelope &envelope, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "envelope is not a JSON object (E6002)";
        return false;
    }

    // Version: field is additive-forward-read; a missing "v" predates the
    // versioned contract and is refused like any other major mismatch.
    const Json::Value &version = json[ "v" ];
    if ( !version.isInt() || version.asInt() != kProtocolVersionMajor )
    {
        const int peer = version.isInt() ? version.asInt() : 0;
        error = "envelope protocol major " + std::to_string( peer )
                + " does not match supported major "
                + std::to_string( kProtocolVersionMajor ) + " (E6001)";
        return false;
    }

    const Json::Value &typeValue = json[ "type" ];
    if ( !typeValue.isString()
         || !messageTypeFromName( typeValue.asString(), envelope.type ) )
    {
        error = "unknown envelope type (E6002)";
        return false;
    }

    const Json::Value &id = json[ "id" ];
    if ( !id.isNull() )
    {
        if ( !id.isInt64() && !id.isUInt() && !id.isInt() )
        {
            error = "envelope id is not an integer (E6002)";
            return false;
        }
        envelope.id = id.asInt64();
    }

    switch ( envelope.type )
    {
    case MessageType::Request:
    {
        const Json::Value &method = json[ "method" ];
        if ( !method.isString() || method.asString().empty() )
        {
            error = "request envelope without method (E6002)";
            return false;
        }
        envelope.method = method.asString();
        envelope.params = json.get( "params", Json::Value( Json::objectValue ) );
        if ( !envelope.params.isObject() && !envelope.params.isNull() )
        {
            error = "request params must be an object (E6002)";
            return false;
        }
        const Json::Value &deadline = json[ "deadlineMs" ];
        envelope.deadlineMs = deadline.isInt() ? deadline.asInt() : 0;
        break;
    }
    case MessageType::Response:
    {
        const Json::Value &ok = json[ "ok" ];
        if ( !ok.isBool() )
        {
            error = "response envelope without ok flag (E6002)";
            return false;
        }
        envelope.ok = ok.asBool();
        if ( envelope.ok )
        {
            envelope.result = json.get( "result", Json::Value( Json::objectValue ) );
        }
        else
        {
            const Json::Value &errorValue = json[ "error" ];
            if ( !errorValue.isObject() )
            {
                error = "failed response without error object (E6002)";
                return false;
            }
            envelope.error = IpcError::fromJson( errorValue );
            if ( envelope.error.code.empty() )
            {
                error = "failed response without error code (E6002)";
                return false;
            }
        }
        break;
    }
    case MessageType::Progress:
    {
        const Json::Value &value = json[ "value" ];
        if ( !value.isNumeric() )
        {
            error = "progress envelope without numeric value (E6002)";
            return false;
        }
        envelope.progress = value.asDouble();
        const Json::Value &message = json[ "message" ];
        if ( message.isString() )
            envelope.message = message.asString();
        break;
    }
    case MessageType::Cancel:
        break;
    case MessageType::Event:
    {
        const Json::Value &event = json[ "event" ];
        if ( !event.isString() || event.asString().empty() )
        {
            error = "event envelope without event name (E6002)";
            return false;
        }
        envelope.method = event.asString();
        envelope.params = json.get( "params", Json::Value( Json::objectValue ) );
        break;
    }
    }
    return true;
}

bool decodeEnvelopePayload( const std::string &payload, Envelope &envelope, std::string &error )
{
    Json::Value json;
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    std::string parseError;
    if ( !reader->parse( payload.data(), payload.data() + payload.size(), &json, &parseError ) )
    {
        error = "frame payload is not valid JSON: " + parseError + " (E6002)";
        return false;
    }
    return decodeEnvelope( json, envelope, error );
}

Envelope makeErrorResponse( long long id, const std::string &code, const std::string &message,
                            bool retryable, Json::Value data )
{
    Envelope envelope;
    envelope.type = MessageType::Response;
    envelope.id = id;
    envelope.ok = false;
    envelope.error.code = code;
    envelope.error.message = message;
    envelope.error.retryable = retryable;
    envelope.error.data = std::move( data );
    return envelope;
}

} // namespace Ipc

} // namespace exprs
