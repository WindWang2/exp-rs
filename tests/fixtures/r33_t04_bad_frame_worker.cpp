#include <json/json.h>

#include <iostream>
#include <string>

namespace
{
void send( Json::Value frame, const char *op )
{
    frame["v"] = 1;
    frame["op"] = op;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::cout << Json::writeString( writer, frame ) << std::endl;
}
} // namespace

int main()
{
    Json::Value ready( Json::objectValue );
    ready["caps"].append( "progress" );
    ready["caps"].append( "cancelAck" );
    send( ready, "ready" );
    for ( std::string line; std::getline( std::cin, line ); )
    {
        Json::Value request;
        Json::Reader reader;
        if ( !reader.parse( line, request, false ) || !request.isObject() )
            continue;
        const std::string op = request.get( "op", "" ).asString();
        if ( op == "shutdown" )
            break;
        Json::Value response( Json::objectValue );
        response["jobId"] = request.get( "jobId", "" );
        if ( op == "cancel" )
        {
            response["kind"] = "cancel";
            send( response, "ack" );
            response.removeMember( "kind" );
            response["message"] = "cancelled";
            response["code"] = "cancelled";
            send( response, "error" );
        }
        if ( op != "run" )
            continue;

        const std::string algorithm = request.get( "algorithmId", "" ).asString();
        if ( algorithm == "r33t04:bad_progress" )
        {
            response["value"] = "high";
            send( response, "progress" );
        }
        else if ( algorithm == "r33t04:bad_result" )
        {
            response["jobId"] = 5;
            response["payload"] = Json::Value( Json::objectValue );
            send( response, "result" );
        }
        else if ( algorithm == "r33t04:ok" )
        {
            response["value"] = 0.5;
            send( response, "progress" );
            response.removeMember( "value" );
            response["payload"]["status"] = "ok";
            send( response, "result" );
        }
        else
        {
            response["message"] = "unknown algorithm: " + algorithm;
            send( response, "error" );
        }
    }
}
