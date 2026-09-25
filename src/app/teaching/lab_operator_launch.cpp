// src/app/teaching/lab_operator_launch.cpp
#include "lab_operator_launch.h"

#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/schema_validator.h"
#include "operators/framework/rs_operator_registry.h"

#include <QDir>

#include <functional>
#include <memory>
#include <string_view>

namespace sicnu::app::teaching {
namespace {

Json::Value parseParamsDocument( const QString &paramsJson, std::string &error )
{
  Json::Value root;
  if ( paramsJson.trimmed().isEmpty() ) return Json::Value( Json::objectValue );
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  const std::string bytes = paramsJson.toStdString();
  if ( !reader->parse( bytes.data(), bytes.data() + bytes.size(), &root, &error ) )
    return Json::Value();
  return root;
}

} // namespace

LabOperatorLaunchPlan prepareLabOperatorLaunch( const QString &operatorId,
                                                const QString &paramsJson )
{
  LabOperatorLaunchPlan plan;

  const std::string opId = operatorId.toStdString();
  const std::string paramsText = paramsJson.toStdString();
  const bool hasParams = !paramsJson.trimmed().isEmpty();

  Json::Value params;
  if ( hasParams ) {
    std::string parseError;
    params = parseParamsDocument( paramsJson, parseError );
    if ( params.isNull() ) {
      plan.issuesZh.push_back( "步骤参数不是合法 JSON: " + parseError );
      return plan;
    }
    if ( !params.isObject() ) {
      plan.issuesZh.push_back( "步骤参数必须是 JSON 对象，收到的是 "
                               + std::string( params.isString() ? "字符串" : "其他类型" ) );
      return plan;
    }
  }

  // No params → nothing to check or prefill: the operator surface opens with
  // its own defaults (the pre-launch status quo, now explicit).
  if ( !hasParams || params.empty() ) {
    plan.ok = true;
    plan.params = Json::Value( Json::objectValue );
    return plan;
  }

  // Params present → the runtime registry is the only parameter authority.
  // An operator it does not know would have its params checked nowhere, so
  // both injecting and silently dropping would lie: refuse fail-closed.
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( opId );
  if ( !op ) {
    plan.issuesZh.push_back( "运行时注册表未注册算子 " + opId
                             + "，无法校验步骤参数（fail-closed）" );
    return plan;
  }

  const sicnu::processing::AlgorithmDescriptor desc =
    sicnu::processing::AlgorithmDescriptorBuilder::buildFromRsOperator( *op );
  const sicnu::processing::ParameterValidationResult validation =
    sicnu::processing::validateParameters( params, desc,
                                           sicnu::processing::UnknownParameterPolicy::Error );
  if ( !validation.ok() ) {
    for ( const auto &issue : validation.errors ) {
      std::string reason = "参数 " + issue.parameter + " 被拒绝: " + issue.message;
      if ( !issue.expected.empty() ) reason += "（期望 " + issue.expected;
      if ( !issue.expected.empty() && !issue.actual.empty() ) reason += "，实际 " + issue.actual;
      if ( !issue.expected.empty() ) reason += "）";
      plan.issuesZh.push_back( std::move( reason ) );
    }
    return plan;
  }

  plan.ok = true;
  plan.params = params;
  return plan;
}

Json::Value absolutizeLabInputPaths( const Json::Value &params, const QString &dataRoot )
{
  if ( !params.isObject() || dataRoot.isEmpty() ) return params;
  std::function<Json::Value( const Json::Value & )> walk =
    [&]( const Json::Value &node ) -> Json::Value {
    if ( node.isString() ) {
      const std::string &value = node.asString();
      static constexpr std::string_view kDataPrefix = "data/";
      if ( value.starts_with( kDataPrefix ) )
        return Json::Value( QDir( dataRoot )
                              .filePath( QString::fromStdString(
                                value.substr( kDataPrefix.size() ) ) )
                              .toStdString() );
      return node;
    }
    if ( node.isArray() ) {
      Json::Value out( Json::arrayValue );
      for ( const auto &item : node ) out.append( walk( item ) );
      return out;
    }
    if ( node.isObject() ) {
      Json::Value out( Json::objectValue );
      for ( const auto &key : node.getMemberNames() ) out[key] = walk( node[key] );
      return out;
    }
    return node;
  };
  return walk( params );
}

Json::Value parseLabPrefillParams( const QString &paramsJson, std::string *error )
{
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  const std::string bytes = paramsJson.toStdString();
  std::string localError;
  const bool parsed = reader->parse( bytes.data(), bytes.data() + bytes.size(),
                                     &root, &localError );
  if ( !parsed ) {
    if ( error ) *error = localError;
    return Json::Value();
  }
  if ( !root.isObject() ) {
    if ( error ) *error = "prefill params must be a JSON object";
    return Json::Value();
  }
  return root;
}

} // namespace sicnu::app::teaching
