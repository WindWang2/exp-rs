// rs_edit_agent_tool.h — F11 Package F: agent-visible editing facts.
//
// READ-ONLY by design (GOAL envelope F): the `editing:state` SpatialTool
// publishes stable edit-session facts so agents can perceive editing state;
// there is deliberately NO agent-callable path that mutates layers — write
// operations stay explicit app commands with undo/permission semantics.
//
// Concurrency note: like the WorkbenchContextTool precedent, execute() reads
// GUI-thread-owned objects; in-process callers dispatch it on the main
// thread (the MCP server tool path already does).
#pragma once

#include "agent/spatial_tools/spatial_tool.h"

#include <QPointer>
#include <QObject>

class RsEditSession;
class RsSnappingController;

/// Read-only SpatialTool over live editing objects. Not registered
/// automatically — the workbench mount registers it into the single
/// SpatialToolRegistry with these guarded sources.
class RsEditAgentTool : public QObject, public sicnu::agent::spatial_tools::SpatialTool
{
    Q_OBJECT

  public:
    static constexpr const char *kToolId = "editing:state";

    struct Sources
    {
        QPointer<RsEditSession> session;
        QPointer<RsSnappingController> snapping;
    };

    explicit RsEditAgentTool( Sources sources, QObject *parent = nullptr );

    // SpatialTool interface (read-only).
    std::string name() const override { return kToolId; }
    std::string displayName() const override;
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    sicnu::agent::spatial_tools::SpatialToolResult execute( const Json::Value &input ) override;

    /// Payload assembly used by execute(); exposed for tests. Returns the
    /// inner "editing" object (empty object when no live session).
    Json::Value buildPayload() const;

  private:
    Sources mSources;
};
