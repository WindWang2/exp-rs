/***************************************************************************
 * src/plugins/framework/plugin_operator_adapter.cpp
 ***************************************************************************/
#include "plugin_operator_adapter.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

namespace sicnu::plugins {

namespace {
Json::Value executionEstimateFromManifest( const exprs::ManifestOperator &op )
{
    Json::Value estimate = op.metadata.isObject() ? op.metadata.get( "execution", Json::Value( Json::objectValue ) )
                                                  : Json::Value( Json::objectValue );
    return estimate;
}
} // namespace

sicnu::processing::PortDescriptor manifestPortToPortDescriptor( const exprs::ManifestPort &port )
{
    sicnu::processing::PortDescriptor descriptor;
    descriptor.name = port.name;
    descriptor.displayName = port.name;
    descriptor.description = port.description;
    descriptor.type = sicnu::processing::dataTypeFromString( port.type );
    descriptor.required = port.required;
    if ( !port.defaultValue.isNull() && port.defaultValue.isConvertibleTo( Json::stringValue ) )
        descriptor.defaultValue = port.defaultValue.asString();
    if ( port.enumOptions.isArray() )
    {
        for ( const Json::Value &option : port.enumOptions )
        {
            if ( option.isString() )
                descriptor.enumOptions.push_back( option.asString() );
        }
    }
    descriptor.hasMinimum = port.hasMin;
    descriptor.minimum = port.minValue;
    descriptor.hasMaximum = port.hasMax;
    descriptor.maximum = port.maxValue;
    descriptor.fileFormat = port.fileFormat;
    return descriptor;
}

PluginOperatorAdapter::PluginOperatorAdapter( exprs::ManifestOperator manifestOperator,
                                              OperatorFactory lazyFactory,
                                              std::function<bool()> ensurePluginLoaded )
    : mManifest( std::move( manifestOperator ) )
    , mFactory( std::move( lazyFactory ) )
    , mEnsureLoaded( std::move( ensurePluginLoaded ) )
{
    mDescriptorCache = buildDescriptor();
}

PluginOperatorAdapter::PluginOperatorAdapter(
    sicnu::processing::AlgorithmDescriptor precomputedDescriptor, OperatorFactory lazyFactory,
    std::function<bool()> ensurePluginLoaded )
    : mFactory( std::move( lazyFactory ) )
    , mEnsureLoaded( std::move( ensurePluginLoaded ) )
{
    mDescriptorCache = std::move( precomputedDescriptor );
}

sicnu::processing::AlgorithmDescriptor PluginOperatorAdapter::buildDescriptor() const
{
    sicnu::processing::AlgorithmDescriptor descriptor;
    descriptor.id = mManifest.id;
    descriptor.displayName = mManifest.displayName;
    descriptor.group = mManifest.group.empty() ? "plugin" : mManifest.group;
    descriptor.description = mManifest.description;
    for ( const exprs::ManifestPort &port : mManifest.inputs )
        descriptor.inputs.push_back( manifestPortToPortDescriptor( port ) );
    for ( const exprs::ManifestPort &port : mManifest.outputs )
        descriptor.outputs.push_back( manifestPortToPortDescriptor( port ) );
    if ( mManifest.metadata.isObject() )
    {
        // AgentMetadata is descriptor-authoritative; manifest metadata is
        // informational and merged best-effort.
        if ( mManifest.metadata.isMember( "purpose" ) && mManifest.metadata["purpose"].isString() )
            descriptor.agentMetadata.purpose = mManifest.metadata["purpose"].asString();
        if ( mManifest.metadata.isMember( "tags" ) && mManifest.metadata["tags"].isArray() )
        {
            for ( const Json::Value &tag : mManifest.metadata["tags"] )
            {
                if ( tag.isString() )
                    descriptor.agentMetadata.tags.push_back( tag.asString() );
            }
        }
    }
    return descriptor;
}

sicnu::processing::AlgorithmDescriptor PluginOperatorAdapter::descriptor() const
{
    if ( mDescriptorCache.id.empty() )
        mDescriptorCache = buildDescriptor();
    return mDescriptorCache;
}

Json::Value PluginOperatorAdapter::execute( const Json::Value &params,
                                            sicnu::processing::ProgressCallback progressCb,
                                            std::function<bool()> isCancelledFn )
{
    if ( !mFactory || !mEnsureLoaded || !mEnsureLoaded() )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        failure["error"] = "plugin operator '" + mManifest.id
                           + "' is not available (plugin failed to load or is missing)";
        return failure;
    }

    std::unique_ptr<sicnu::operators::RSOperator> operatorInstance;
    try
    {
        operatorInstance = mFactory();
    }
    catch ( const std::exception &exception )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        failure["error"] = std::string( "operator factory threw: " ) + exception.what();
        return failure;
    }
    if ( !operatorInstance )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        failure["error"] = "operator factory returned nullptr";
        return failure;
    }

    sicnu::operators::RSOperatorContext context;
    if ( progressCb )
    {
        context.setProgressCallback(
            [progressCb]( double progress, const std::string &message ) {
                progressCb( static_cast<int>( progress * 100.0 ), message );
            } );
    }
    if ( isCancelledFn )
        context.setCancelCallback( isCancelledFn );
    context.setLogCallback( []( const std::string &message, const std::string &level ) {
        (void)message;
        (void)level;
    } );

    // The RSOperator contract: run() returns a result JSON object and throws
    // RSOperatorError on failure.
    Json::Value result;
    try
    {
        result = operatorInstance->run( params, context );
    }
    catch ( const sicnu::operators::RSOperatorError &error )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        failure["cancelled"] = error.code() == sicnu::operators::ErrorCode::Cancelled;
        failure["error"] = error.message();
        failure["details"] = error.toJson();
        return failure;
    }
    catch ( const std::exception &exception )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        failure["error"] = std::string( "operator run() threw: " ) + exception.what();
        return failure;
    }
    if ( isCancelledFn && isCancelledFn() )
    {
        Json::Value cancelled( Json::objectValue );
        cancelled["success"] = false;
        cancelled["cancelled"] = true;
        return cancelled;
    }
    return result;
}

Json::Value PluginOperatorAdapter::estimateExecution( const Json::Value & ) const
{
    return executionEstimateFromManifest( mManifest );
}

} // namespace sicnu::plugins
