// src/agent/copilot_tool_gate.h
#pragma once

//
// GUI Copilot single-tool-call gate (#1763 / AS-014).
//
// The MCP surface sandboxes every tool call (workspace containment +
// default-deny remote). The GUI Copilot's ToolCallDispatcher path did not:
// single calls parsed from model output ran directly, so prompt injection
// via tool results or dataset metadata could drive arbitrary-path
// read/write or /vsicurl fetches. This gate applies the SAME policy
// authority (tool_catalog/workspace_containment.h) to the copilot's
// envelopes before they are submitted, and classifies write-shaped calls
// so the dock can route them through the existing human approval card
// instead of executing them silently.
//
// Header-only (Qt Core + jsoncpp), testable without the widget: the same
// reason the containment policy itself is a header.
//

#include "agent/tool_catalog/workspace_containment.h"

#include <json/json.h>
#include <QString>
#include <QStringList>
#include <QSet>

namespace sicnu::agent::copilot_gate {

/// Free-text argument slots: caller prose that tools persist or echo, never
/// a filesystem slot the tool opens. Mirrors the MCP surface's
/// isFreeTextSlot set (mcp_server.cpp) — exact key names, unknown keys stay
/// scanned (fail-closed).
inline bool isFreeTextSlot( const QString &key )
{
    static const QSet<QString> kFreeText = {
        QStringLiteral( "note" ),      QStringLiteral( "notes" ),
        QStringLiteral( "error_message" ), QStringLiteral( "errorMessage" ),
        QStringLiteral( "error_code" ),    QStringLiteral( "errorCode" ),
        QStringLiteral( "provenance" ),    QStringLiteral( "goal" ),
        QStringLiteral( "intent" ),        QStringLiteral( "description" ),
        QStringLiteral( "reason" ),        QStringLiteral( "detail" ),
        QStringLiteral( "details" ),       QStringLiteral( "message" ),
        QStringLiteral( "comment" ),       QStringLiteral( "comments" ),
        QStringLiteral( "summary" ),       QStringLiteral( "prompt" ),
        QStringLiteral( "instructions" ),  QStringLiteral( "text" ),
        QStringLiteral( "label" ),         QStringLiteral( "title" ),
    };
    return kFreeText.contains( key );
}

/// Walks the envelope's argument object and reports the FIRST string value
/// that violates the workspace containment / remote default-deny policy.
/// Empty return = clean.
inline QString containmentViolation( const Json::Value &value, const QString &workspaceRoot,
                                     const QString &key = {} )
{
    if ( !key.isEmpty() && isFreeTextSlot( key ) )
        return {};
    if ( value.isString() )
    {
        QString detail;
        if ( tool_catalog::containment::pathOutsideWorkspace( QString::fromStdString(
                                                                  value.asString() ),
                                                              workspaceRoot, &detail ) )
            return detail.isEmpty() ? QStringLiteral( "Path outside the workspace" ) : detail;
        return {};
    }
    if ( value.isArray() )
    {
        for ( const auto &element : value )
        {
            const QString violation = containmentViolation( element, workspaceRoot, key );
            if ( !violation.isEmpty() )
                return violation;
        }
        return {};
    }
    if ( value.isObject() )
    {
        for ( const auto &member : value.getMemberNames() )
        {
            const QString violation =
                containmentViolation( value[member], workspaceRoot, QString::fromStdString( member ) );
            if ( !violation.isEmpty() )
                return violation;
        }
    }
    return {};
}

/// Argument slots that name an OUTPUT artifact: a call carrying one of
/// these writes to the filesystem, and a write by unreviewed model output
/// deserves the same human Confirm the plan flow already requires.
inline bool isWriteSlot( const QString &key )
{
    static const QSet<QString> kWrite = {
        QStringLiteral( "output" ),       QStringLiteral( "output_path" ),
        QStringLiteral( "output_raster" ), QStringLiteral( "output_vector" ),
        QStringLiteral( "out_path" ),     QStringLiteral( "out" ),
        QStringLiteral( "dest" ),         QStringLiteral( "destination" ),
        QStringLiteral( "save_path" ),    QStringLiteral( "save_to" ),
        QStringLiteral( "target_path" ),  QStringLiteral( "write_to" ),
    };
    return kWrite.contains( key );
}

inline bool valueTreeIsWriteShaped( const Json::Value &value, const QString &key = {} )
{
    if ( !key.isEmpty() && isWriteSlot( key ) )
    {
        const bool namesSomething =
            ( value.isString() && !value.asString().empty() ) || value.isObject() || value.isArray();
        if ( namesSomething )
            return true;
    }
    if ( value.isObject() )
    {
        for ( const auto &member : value.getMemberNames() )
        {
            if ( valueTreeIsWriteShaped( value[member], QString::fromStdString( member ) ) )
                return true;
        }
    }
    else if ( value.isArray() )
    {
        for ( const auto &element : value )
        {
            if ( valueTreeIsWriteShaped( element, key ) )
                return true;
        }
    }
    return false;
}

/// The gate verdict for one copilot tool-call envelope ({name, parameters}
/// shape — the dispatcher's parseEnvelope normalizes before this point, but
/// the gate reads the raw envelope defensively).
struct SingleCallGate
{
    /// Non-empty when the call must be REFUSED (containment/remote policy).
    QString refusalReason;
    /// True when the call is a filesystem write and needs human approval
    /// before dispatch.
    bool needsApproval = false;
};

inline SingleCallGate gateSingleCall( const Json::Value &envelope )
{
    SingleCallGate verdict;
    const QString root = tool_catalog::containment::effectiveMcpWorkspaceRoot();
    if ( envelope.isObject() )
    {
        for ( const char *argKey : { "parameters", "arguments", "params" } )
        {
            if ( envelope.isMember( argKey ) )
            {
                verdict.refusalReason = containmentViolation( envelope[argKey], root );
                if ( valueTreeIsWriteShaped( envelope[argKey] ) )
                    verdict.needsApproval = true;
                break;
            }
        }
    }
    return verdict;
}

} // namespace sicnu::agent::copilot_gate
