/***************************************************************************
 * src/plugins/host/plugin_host_operators_shim.cpp
 *
 * Link shim for the launcher-side host-process module (and the worker exe).
 * RSOperator's non-inline default virtual implementations live in the FULL
 * operators library (sicnu_operators, which drags Qt/QGIS); the isolation
 * modules deliberately link only sicnu_operators_core (the contract leaf).
 * The proxies override every defaulted virtual, but the C++ ABI still needs
 * the base symbols emitted for construction vtables — so they are provided
 * here, exactly once per linking module, with the neutral defaults from
 * rs_operator.h. Not used by the in-process plugin path (which links the
 * full operators library as before).
 ***************************************************************************/
#include "operators/framework/rs_operator.h"

namespace sicnu {
namespace operators {

std::string RSOperator::displayName() const
{
    return name();
}
std::string RSOperator::group() const
{
    return "general";
}
std::string RSOperator::description() const
{
    return {};
}
Json::Value RSOperator::schema() const
{
    return Json::Value( Json::objectValue );
}
Json::Value RSOperator::metadata() const
{
    return Json::Value( Json::objectValue );
}
Json::Value RSOperator::executionEstimate() const
{
    return Json::Value( Json::objectValue );
}
Json::Value RSOperator::estimateExecution( const Json::Value & ) const
{
    return Json::Value( Json::objectValue );
}

} // namespace operators
} // namespace sicnu
