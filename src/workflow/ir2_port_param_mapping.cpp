// src/workflow/ir2_port_param_mapping.cpp — IR2 inbound port → RSOperator params (D18)
#include "workflow/ir2_port_param_mapping.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace sicnu::workflow {
namespace {

bool paramStringEmpty( const Json::Value &params, const char *key )
{
    return !params.isMember( key ) || !params[key].isString() || params[key].asString().empty();
}

void setParamIfEmpty( Json::Value &params, const QString &key, const QString &path )
{
    if ( path.isEmpty() )
        return;
    const std::string stdKey = key.toStdString();
    if ( paramStringEmpty( params, stdKey.c_str() ) )
        params[stdKey] = path.toStdString();
}

} // namespace

void applyIr2InputPortMapping( const NodeFact &node,
                               const QHash<QString, QString> &inputArtifacts,
                               Json::Value &params )
{
    if ( inputArtifacts.isEmpty() )
        return;

    QSet<QString> declaredPorts;
    for ( const PortFact &port : node.inputPorts )
        if ( !port.portName.trimmed().isEmpty() )
            declaredPorts.insert( port.portName );

    QStringList keys = inputArtifacts.keys();
    std::sort( keys.begin(), keys.end() );

    QHash<QString, QString> byPort;
    QStringList legacyKeys;

    for ( const QString &key : keys )
    {
        const QString path = inputArtifacts.value( key );
        if ( path.isEmpty() )
            continue;
        // Prefer explicit port-name keys. When the node declares no input
        // ports, treat every key as a param name (designer / ad-hoc cases).
        if ( declaredPorts.isEmpty() || declaredPorts.contains( key ) )
            byPort.insert( key, path );
        else
            legacyKeys.append( key );
    }

    // Order-only fallback for legacy sourceNodeId-keyed maps: zip sorted
    // unknown keys onto declared input ports in declaration order.
    if ( byPort.isEmpty() && !legacyKeys.isEmpty() && !node.inputPorts.isEmpty() )
    {
        int i = 0;
        for ( const PortFact &port : node.inputPorts )
        {
            if ( port.portName.trimmed().isEmpty() )
                continue;
            if ( i >= legacyKeys.size() )
                break;
            byPort.insert( port.portName, inputArtifacts.value( legacyKeys.at( i ) ) );
            ++i;
        }
    }
    else if ( byPort.isEmpty() && !legacyKeys.isEmpty() )
    {
        // No declared ports and non-port keys: first → "input", rest keep key.
        byPort.insert( QStringLiteral( "input" ), inputArtifacts.value( legacyKeys.front() ) );
        for ( int i = 1; i < legacyKeys.size(); ++i )
            byPort.insert( legacyKeys.at( i ), inputArtifacts.value( legacyKeys.at( i ) ) );
    }
    else if ( !legacyKeys.isEmpty() )
    {
        // Mixed: port-named bindings already preferred; leftover legacy keys
        // fill remaining unbound declared ports in declaration order.
        int i = 0;
        for ( const PortFact &port : node.inputPorts )
        {
            if ( port.portName.trimmed().isEmpty() || byPort.contains( port.portName ) )
                continue;
            if ( i >= legacyKeys.size() )
                break;
            byPort.insert( port.portName, inputArtifacts.value( legacyKeys.at( i ) ) );
            ++i;
        }
    }

    Json::Value artifacts( Json::objectValue );
    QStringList portKeys = byPort.keys();
    std::sort( portKeys.begin(), portKeys.end() );
    for ( const QString &portName : portKeys )
    {
        const QString path = byPort.value( portName );
        artifacts[portName.toStdString()] = path.toStdString();
        setParamIfEmpty( params, portName, path );
    }
    if ( !byPort.isEmpty() )
        params["ir2_input_artifacts"] = artifacts;

    if ( paramStringEmpty( params, "input" ) )
    {
        if ( byPort.contains( QStringLiteral( "input" ) ) )
        {
            params["input"] = byPort.value( QStringLiteral( "input" ) ).toStdString();
        }
        else
        {
            bool set = false;
            for ( const PortFact &port : node.inputPorts )
            {
                if ( byPort.contains( port.portName ) )
                {
                    params["input"] = byPort.value( port.portName ).toStdString();
                    set = true;
                    break;
                }
            }
            if ( !set && !portKeys.isEmpty() )
                params["input"] = byPort.value( portKeys.front() ).toStdString();
        }
    }
}

} // namespace sicnu::workflow
