/***************************************************************************
 * src/plugins/framework/external_tool_operator.h
 *
 * RSOperator binding for manifest-declared external executables
 * (plugin manifest "external" section, and the generic `ext:run` operator).
 * Wraps exprs::ExternalProcess with the operator contract:
 *   - ${param} substitution from the params object (argv-only, never shell)
 *   - declared outputs are redirected to temp paths and published
 *     transactionally (rename) after a clean exit
 *   - progress derived from stdout activity; cancel -> process-group kill
 *   - bounded stdout/stderr capture; exit code surfaced in the result
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"
#include "exprs/plugin_manifest.h"

#include <map>

namespace sicnu::plugins {

/// Operator instance bound to one manifest external tool declaration.
class ExternalToolOperator : public sicnu::operators::RSOperator
{
public:
    ExternalToolOperator( std::string operatorId, exprs::ManifestOperator declaration,
                          std::string pluginDir = {} );

    std::string name() const override { return mOperatorId; }
    std::string displayName() const override { return mDeclaration.displayName; }
    std::string group() const override { return mDeclaration.group.empty() ? "plugin" : mDeclaration.group; }
    std::string description() const override { return mDeclaration.description; }
    std::string determinismGrade() const override { return mDeclaration.determinismGrade; }
    sicnu::operators::RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return sicnu::operators::RSOperatorMemoryPolicy::ExternalProcess;
    }
    Json::Value schema() const override { return mDeclaration.schema.isNull() ? Json::Value( Json::objectValue ) : mDeclaration.schema; }
    Json::Value metadata() const override { return mDeclaration.metadata; }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override;

private:
    /// Substitutes "${name}" placeholders from @p params (and output-port
    /// temp redirections). Returns false when a required placeholder is
    /// missing.
    bool buildArgv( const Json::Value &params, sicnu::operators::RSOperatorContext &context,
                    std::vector<std::string> &argv,
                    std::map<std::string, std::pair<std::string, std::string>> &outputMoves,
                    std::string &error ) const;

    std::string mOperatorId;
    exprs::ManifestOperator mDeclaration;
    std::string mPluginDir;
};

} // namespace sicnu::plugins
