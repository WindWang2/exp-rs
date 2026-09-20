// src/workflow/workflow_ir_v2.h — Workflow IR 2.0: the typed visual-pipeline document (D17, ADR 0162)
#pragma once

//
// Workflow IR 2.0 is the Qt-native property-graph document behind the
// node-based pipeline designer. Where the ADR 0149 WorkflowIR 1.0
// (sicnu::agent::harness, std::string/jsoncpp) is the agent-side compiler
// document, the 2.0 layer is the designer-side document: value types,
// canvas geometry, typed ports, and byte-stable JSON round-trips.
//
// Formal contract (pinned by tests/test_workflow_ir_v2.cpp):
//   - Property graph: every edge references existing nodes AND existing
//     port names on both endpoints (validateSemantics, fail-closed).
//   - Single-source invariant: an input port has in-degree <= 1.
//   - Round-trip idempotence: for CANONICAL documents (every field
//     materialized — the form toJson emits) S(D(J)) == J byte-for-byte
//     under QJsonDocument::Indented; D(S(A)) == A always holds.
//   - Migration: migrateFromV1 lifts an ADR 0149 "workflow_ir" 1.0
//     document, defaulting radiometricState and canvasPosition; malformed
//     V1 fails closed.
//
// Layering: Qt Core/Gui only; no qgis, no processing, no agent deps.
//

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QVector>

#include <utility>

namespace sicnu::workflow {

/// Minimal expected-like result. No exceptions cross this seam.
template <typename T>
class [[nodiscard]] Result
{
  public:
    static Result ok( T value ) { Result r; r.m_ok = true; r.m_value = std::move( value ); return r; }
    static Result error( QString message ) { Result r; r.m_error = std::move( message ); return r; }

    bool isSuccess() const { return m_ok; }
    const T &value() const { return m_value; }
    T &value() { return m_value; }
    const QString &error() const { return m_error; }

  private:
    bool m_ok = false;
    T m_value{};
    QString m_error;
};

/// One typed port: the data contract of what flows through it.
struct PortFact
{
    QString portName;
    QString dataType;         ///< "Raster" | "Vector" | "Table" | "Scalar" | "Mask"
    QString crs;              ///< "EPSG:32649" | "EPSG:4326" | "*" (any)
    QString radiometricState; ///< "DN" | "Radiance" | "TOA" | "BOA" | "Index" | "Mask" | "*" | "None"
    double resolutionX = 0.0;
    double resolutionY = 0.0;
    int bandCount = 0;
    bool isRequired = true;

    bool operator==( const PortFact & ) const = default;
};

/// One node: an operator invocation with typed ports and canvas geometry.
struct NodeFact
{
    QString nodeId;
    QString operatorId;       ///< e.g. "rs:radiometric_calibration"
    QString displayName;
    QJsonObject parameters;
    QVector<PortFact> inputPorts;
    QVector<PortFact> outputPorts;
    QPointF canvasPosition;
    /// Schema 2.1, additive-optional: for a node materialized by subflow
    /// expansion, the designer-level fragment-instance node it came from
    /// (empty for authored nodes). Serialized only when non-empty so 2.0
    /// documents stay byte-identical; consumed by error attribution and
    /// provenance.
    QString originNodeId;

    bool operator==( const NodeFact & ) const = default;
};

/// One wiring edge: (sourceNode, sourcePort) -> (targetNode, targetPort).
struct EdgeFact
{
    QString edgeId;
    QString sourceNodeId;
    QString sourcePortName;
    QString targetNodeId;
    QString targetPortName;

    bool operator==( const EdgeFact & ) const = default;
};

/// The versioned workflow document — single source of truth for the canvas,
/// the guided workbench, the optimizer and the run coordinator.
/// Named `WorkflowDocument`, not `WorkflowDefinition`: Engine 2.0 already owns
/// `sicnu::workflow::WorkflowDefinition` (`workflow_types.h`, std::string
/// based) and the two collide in any TU that includes both headers.
struct WorkflowDocument
{
    /// Claimed schema version; must be a member of
    /// WorkflowIR::supportedSchemaVersions() to parse. New documents default
    /// to the current version.
    QString version = QStringLiteral( "2.1" );
    QString workflowId;
    QString name;
    QString description;
    QVector<NodeFact> nodes;
    QVector<EdgeFact> edges;
    QJsonObject metadata;

    bool isValid() const;
    const NodeFact *findNode( const QString &id ) const;
    const EdgeFact *findEdge( const QString &id ) const;

    bool operator==( const WorkflowDocument & ) const = default;
};

/// The IR 2.0 seam: parsing, serialization, semantic validation, V1 lift.
class WorkflowIR
{
  public:
    WorkflowIR() = delete;

    /// Schema versions this parser accepts, oldest first: {"2.0", "2.1"}.
    /// "2.1" adds the optional NodeFact::originNodeId — additive-optional
    /// fields are the sanctioned intra-family extension mechanism: a 2.0
    /// writer never emits them, and a 2.1 reader tolerates their absence.
    /// Anything outside this set is refused with the offending version
    /// named — an older build rejects a newer document *explicitly* instead
    /// of silently dropping fields it does not understand.
    static QStringList supportedSchemaVersions();
    /// The version new documents should claim ("2.1").
    static QString currentSchemaVersion();

    /// Parses a document whose version is a member of supportedSchemaVersions.
    /// The document's claimed version is preserved verbatim — a "2.0" input
    /// round-trips byte-stably as "2.0". Fails closed: unknown/missing
    /// version, malformed node/edge fields, non-object parameters.
    static Result<WorkflowDocument> fromJson( const QJsonObject &doc );

    /// Emits the canonical document (all fields materialized, sorted keys).
    /// `version` is emitted verbatim — the caller decides what the document
    /// claims; a document using 2.1-only fields must carry version "2.1".
    static QJsonObject toJson( const WorkflowDocument &def );

    /// Structural semantics: unique non-empty ids, edges resolve to existing
    /// nodes and ports, input-port in-degree <= 1. On failure and when
    /// @p outError is non-null, sets a human-readable reason naming the
    /// offender.
    static bool validateSemantics( const WorkflowDocument &def, QString *outError = nullptr );

    /// Lifts an ADR 0149 WorkflowIR 1.0 document (kind "workflow_ir",
    /// schema_version "1.0") into the current schema version. Defaults:
    /// radiometricState from the artifact numeric-domain facts (dn -> "DN",
    /// surface_reflectance -> "BOA", toa -> "TOA", else "None");
    /// canvasPosition on a 4-per-row grid.
    static Result<WorkflowDocument> migrateFromV1( const QJsonObject &v1Doc );
};

} // namespace sicnu::workflow
