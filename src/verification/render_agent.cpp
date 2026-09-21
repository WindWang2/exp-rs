// render_agent.cpp — see render_agent.h for why this surface is flat and closed.

#include "verification/render_agent.h"

#include "verification/failure_codes.h"
#include "verification/render_teaching.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

/// One action per failure code, phrased from the code's own replan class so an
/// Agent never has to guess whether retrying is the right move.
std::string actionForCode( const std::string &code )
{
    const std::string hint = failureHintForCode( code );
    switch ( replanClassForCode( code ) )
    {
        case ReplanClass::Retry:
            return code + ": supply the missing input or evidence and re-run this step unchanged. " +
                   hint;
        case ReplanClass::Replan:
            return code + ": the plan itself has to change — repeating it will fail the same way. " +
                   hint;
        case ReplanClass::Abort:
            return code + ": stop and repair the verification specification; retrying cannot "
                          "help. " + hint;
        case ReplanClass::None:
            return code + ": no action is implied by this code. " + hint;
    }
    return code + ": inspect this code before acting on it. " + hint;
}

Json::Value stringArray( const std::vector<std::string> &values )
{
    Json::Value array{ Json::arrayValue };
    for ( const std::string &value : values )
    {
        array.append( value );
    }
    return array;
}

} // namespace

Json::Value AgentView::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["schema"] = schema;
    json["status"] = status;
    json["failure_codes"] = stringArray( failureCodes );
    json["replan_classes"] = stringArray( replanClasses );
    json["suggested_actions"] = stringArray( suggestedActions );
    json["blocking_nodes"] = stringArray( blockingNodes );
    json["first_blocking_node"] = firstBlockingNode;
    return json;
}

AgentView renderAgent( const VerificationReport &report )
{
    AgentView view;

    // Same derivation the teaching view reads: the two surfaces cannot disagree
    // about what happened, because there is only one place that decides it.
    view.status = statusToWire( derivedStatus( report ) );
    view.failureCodes = derivedFailureCodes( report );

    for ( const std::string &code : view.failureCodes )
    {
        view.suggestedActions.push_back( actionForCode( code ) );
        const std::string replanClass = replanClassToWire( replanClassForCode( code ) );
        if ( std::find( view.replanClasses.begin(), view.replanClasses.end(), replanClass ) ==
             view.replanClasses.end() )
        {
            view.replanClasses.push_back( replanClass );
        }
    }
    std::sort( view.replanClasses.begin(), view.replanClasses.end() );

    view.blockingNodes = derivedBlockingNodes( report );
    view.firstBlockingNode = view.blockingNodes.empty() ? std::string() : view.blockingNodes.front();

    return view;
}

} // namespace sicnu::verification
