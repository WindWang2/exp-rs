// execution_event_conversion.cpp — see execution_event_conversion.h.
// Moved verbatim from workflow_experiment_adapter.cpp so every recording
// surface (monitor, lab recorder) projects execution truth identically.
#include "execution_event_conversion.h"

#include "workflow/workflow_run.h"

#include <map>
#include <utility>

namespace sicnu::experiment
{

QJsonValue jsonCppToQJson( const Json::Value &value, int depth )
{
    if ( depth > 16 )
        return QJsonValue( QStringLiteral( "…truncated" ) );
    switch ( value.type() )
    {
        case Json::nullValue:
            return QJsonValue( QJsonValue::Null );
        case Json::intValue:
            // QJsonValue stores numbers as double (Qt JSON contract); a
            // definition carrying >2^53 magnitudes is out of contract.
            return QJsonValue( static_cast<double>( value.asInt64() ) );
        case Json::uintValue:
            return QJsonValue( static_cast<double>( value.asUInt64() ) );
        case Json::realValue:
            return QJsonValue( value.asDouble() );
        case Json::stringValue:
            return QJsonValue( QString::fromStdString( value.asString() ) );
        case Json::booleanValue:
            return QJsonValue( value.asBool() );
        case Json::arrayValue:
        {
            QJsonArray array;
            const Json::ArrayIndex count = qMin<Json::ArrayIndex>( value.size(), 512 );
            if ( count < value.size() )
                array.append( QStringLiteral( "…truncated" ) );
            for ( Json::ArrayIndex i = 0; i < count; ++i )
                array.append( jsonCppToQJson( value[i], depth + 1 ) );
            return array;
        }
        case Json::objectValue:
        {
            QJsonObject object;
            int written = 0;
            for ( const std::string &key : value.getMemberNames() )
            {
                if ( written++ >= 512 )
                {
                    object.insert( QStringLiteral( "…truncated" ), true );
                    break;
                }
                object.insert( QString::fromStdString( key ),
                               jsonCppToQJson( value[key], depth + 1 ) );
            }
            return object;
        }
    }
    return QJsonValue( QJsonValue::Null );
}

namespace
{

QJsonObject stepSummary( const Json::Value &plan )
{
    QJsonObject summary;
    summary.insert( QStringLiteral( "id" ),
                    QString::fromStdString( plan["stepId"].asString() ) );
    summary.insert( QStringLiteral( "operator" ),
                    QString::fromStdString( plan["operatorId"].asString() ) );
    summary.insert( QStringLiteral( "status" ),
                    QString::fromStdString( plan["status"].asString() ) );
    const Json::Value &error = plan["errorMessage"];
    if ( error.isString() && !error.asString().empty() )
        summary.insert( QStringLiteral( "error" ),
                        QString::fromStdString( error.asString() ) );
    const Json::Value &outputPath = plan["outputLayerPath"];
    if ( outputPath.isString() && !outputPath.asString().empty() )
    {
        QJsonObject output;
        output.insert( QStringLiteral( "path" ),
                       QString::fromStdString( outputPath.asString() ) );
        const Json::Value &size = plan["outputSizeBytes"];
        if ( size.isInt64() && size.asInt64() > 0 )
            output.insert( QStringLiteral( "size" ),
                           static_cast<double>( size.asInt64() ) );
        const Json::Value &digest = plan["outputDigest"];
        if ( digest.isString() && !digest.asString().empty() )
            output.insert( QStringLiteral( "digest" ),
                           QString::fromStdString( digest.asString() ) );
        summary.insert( QStringLiteral( "output" ), output );
    }
    return summary;
}

} // namespace

ExecutionEvent workflowRunToExecutionEvent( const workflow::WorkflowRun &run,
                                            const QString &state, qint64 startedMs,
                                            qint64 finishedMs )
{
    // ONE locked snapshot: the coordinator's fold thread (TaskCenter
    // callbacks) may mutate the aggregate concurrently, and unlocked
    // accessors like definition() deliberately escape the run's mutex —
    // reading them here raced the fold and corrupted the JSON heap. Every
    // value below comes from the single internally-locked serialization.
    const Json::Value snapshot = run.toJson();

    ExecutionEvent event;
    event.executionRef = QString::fromStdString( snapshot["runId"].asString() );
    event.workflowId = QString::fromStdString( snapshot["workflowId"].asString() );
    if ( event.workflowId.isEmpty() )
        event.workflowId =
            QString::fromStdString( snapshot["definition"]["id"].asString() );
    event.state = state;
    event.startedMs = startedMs;
    event.finishedMs = finishedMs;
    const Json::Value &errorMessage = snapshot["errorMessage"];
    if ( errorMessage.isString() )
        event.errorMessage = QString::fromStdString( errorMessage.asString() );
    event.definition = jsonCppToQJson( snapshot["definition"] ).toObject();

    const Json::Value &plans = snapshot["stepPlans"];
    std::map<std::string, std::pair<double, QString>> identityByStep; // completion identity
    for ( const auto &plan : plans )
    {
        event.steps.append( stepSummary( plan ) );
        const Json::Value &digest = plan["outputDigest"];
        const Json::Value &size = plan["outputSizeBytes"];
        if ( digest.isString() && !digest.asString().empty() )
            identityByStep[plan["stepId"].asString()] = std::make_pair(
                size.isInt64() && size.asInt64() > 0 ? static_cast<double>( size.asInt64() )
                                                     : -1.0,
                QString::fromStdString( digest.asString() ) );
    }

    const Json::Value &artifacts = snapshot["artifacts"];
    for ( const std::string &stepId : artifacts.getMemberNames() )
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "path" ),
                      QString::fromStdString( artifacts[stepId].asString() ) );
        const auto identity = identityByStep.find( stepId );
        if ( identity != identityByStep.end() )
        {
            if ( identity->second.first > 0 )
                entry.insert( QStringLiteral( "size" ), identity->second.first );
            if ( !identity->second.second.isEmpty() )
                entry.insert( QStringLiteral( "digest" ), identity->second.second );
        }
        event.artifacts.insert( QString::fromStdString( stepId ), entry );
    }
    return event;
}

} // namespace sicnu::experiment
