// governance_types.h — Workspace Governance 3.0 domain vocabulary.
//
// First-class project entities (Project/Workspace/Asset/Dataset/Result/Run/
// Experiment/Export/Audit) with stable identities that never derive from a
// file path. Paths are storage locators; identity lives here.
//
// Threading: value types only; the store API is documented in
// governance_store.h. Serialization: JSON helpers live next to each entity.
#pragma once

#include "../data_result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

class QUuid;

namespace sicnu::workspace
{

// Reuse the shared diagnostics vocabulary from the data layer.
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

// ---------------------------------------------------------------------------
// Strong ids
// ---------------------------------------------------------------------------

#define SICNU_WORKSPACE_ID_TYPE( Name )                                                       \
    class Name                                                                                \
    {                                                                                         \
      public:                                                                                 \
        Name() = default;                                                                     \
        static Name generate();                                                               \
        static std::optional<Name> fromString( const QString &text );                          \
        bool isNull() const;                                                                  \
        QString toString() const;                                                             \
        friend bool operator==( const Name &, const Name & ) = default;                        \
                                                                                              \
      private:                                                                                \
        explicit Name( QString value );                                                       \
        QString m_value;                                                                      \
    };

SICNU_WORKSPACE_ID_TYPE( WorkspaceId )
SICNU_WORKSPACE_ID_TYPE( DatasetId )
SICNU_WORKSPACE_ID_TYPE( ResultId )
SICNU_WORKSPACE_ID_TYPE( ExperimentId )
SICNU_WORKSPACE_ID_TYPE( SmartCollectionId )
SICNU_WORKSPACE_ID_TYPE( ExportId )

#undef SICNU_WORKSPACE_ID_TYPE

/// Monotonic per-entity revision. Mirrors the AssetRevision contract: any
/// observed content/state change advances the revision by one.
struct EntityRevision
{
    quint64 value = 1;

    EntityRevision next() const { return EntityRevision{ value + 1 }; }
    bool isValid() const { return value != 0; }
    friend bool operator==( const EntityRevision &, const EntityRevision & ) = default;
};

// ---------------------------------------------------------------------------
// Lifecycle vocabularies
// ---------------------------------------------------------------------------

/// Result lifecycle (task §18). Deliberately NOT a permission system.
enum class ResultStatus
{
    Draft,
    Validated,
    Approved,
    Rejected,
    Superseded,
    Archived,
};

QString resultStatusToString( ResultStatus status );
std::optional<ResultStatus> resultStatusFromString( const QString &text );
/// Legal forward transitions; Superseded/Archived are terminal-but-recoverable
/// (archived → draft revalidation is modeled as a new revision, not a rollback).
bool isLegalResultTransition( ResultStatus from, ResultStatus to );

/// Semantic result type (task §17).
enum class ResultSemanticType
{
    Classification,
    ChangeDetection,
    Index,
    Segmentation,
    VectorExtraction,
    Statistics,
    Chart,
    Map,
    Export,
    Other,
};

QString resultSemanticTypeToString( ResultSemanticType type );
std::optional<ResultSemanticType> resultSemanticTypeFromString( const QString &text );

/// Dataset kinds (task §4). Dataset = user/domain dataset; Collection stays the
/// DataManager's observation grouping; Asset stays the physical/logical object.
enum class DatasetKind
{
    SingleRaster,
    MultiBandProduct,
    Temporal,
    Sar,
    Training,
    Reference,
    ModelInput,
    Group,
};

QString datasetKindToString( DatasetKind kind );
std::optional<DatasetKind> datasetKindFromString( const QString &text );

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

/// Common envelope columns shared by every governed entity.
struct EntityHeader
{
    QString name;
    quint64 revision = 1;
    qint64 createdAtMs = 0;
    qint64 updatedAtMs = 0;
    QStringList tags;             ///< free labels; "sys:*" reserved for system tags
    QJsonObject metadata;         ///< opaque structured metadata
};

struct DatasetRecord
{
    DatasetId id;
    DatasetKind kind = DatasetKind::Group;
    EntityHeader header;
    QStringList memberAssetIds;   ///< ordered
};

struct ResultInput
{
    QString assetId;              ///< sicnu::data::AssetId (string form)
    quint64 revision = 0;         ///< 0 = revision not pinned
    QString role;                 ///< e.g. "input", "reference", "model"
};

struct ResultArtifact
{
    QString path;
    QString role;                 ///< "primary", "sidecar", "visualization"
    QString contentDigest;        ///< empty = not computed
    qint64 sizeBytes = -1;
};

struct ResultRecord
{
    ResultId id;
    ResultSemanticType semanticType = ResultSemanticType::Other;
    EntityHeader header;
    ResultStatus status = ResultStatus::Draft;
    QJsonObject producer;         ///< {operatorId, workflowId, runId, stepId, taskId}
    QVector<ResultInput> inputs;
    QVector<ResultArtifact> artifacts;
    QJsonObject metrics;
    QJsonObject quality;
    QString supersededBy;         ///< ResultId string when status == Superseded
    QString validationNotes;
};

struct RunRecord
{
    QString id;                   ///< WorkflowRun runId (filename-safe)
    QString workflowId;
    QString state;
    qint64 startedMs = 0;
    qint64 finishedMs = 0;
    QJsonObject definition;       ///< workflow definition snapshot
    QJsonObject summary;          ///< step summaries / progress
    QStringList outputAssetIds;
    EntityHeader header;
};

struct ExperimentVariant
{
    QString key;
    QJsonObject value;
};

struct ExperimentRecord
{
    ExperimentId id;
    EntityHeader header;
    QString objective;
    QVector<ExperimentVariant> variants;
    QStringList runIds;
};

/// Smart collection predicate: an AND-group of predicates over indexed asset
/// columns. Dynamic — evaluated at query time, never materializes membership.
struct SmartPredicate
{
    QString field;    ///< "sensor","modality","kind","state","crs","tag","year","acquisitionMs","text","format"
    QString op;       ///< "eq","neq","contains","prefix","lt","lte","gt","gte"
    QString value;
};

struct SmartCollectionRecord
{
    SmartCollectionId id;
    EntityHeader header;
    QVector<SmartPredicate> predicates;  ///< AND-combined
};

struct ExportRecord
{
    ExportId id;
    EntityHeader header;
    QString kind;                 ///< "map" | "layout" | "data" | "bundle"
    QString target;               ///< produced path or destination
    QString resultId;             ///< optional provenance link
};

/// Path remapping rule for project relocation (external roots move as a unit).
struct PathMapping
{
    QString kind;                 ///< "externalRoot" | "prefix"
    QString fromPath;
    QString toPath;
};

// ---------------------------------------------------------------------------
// Query model (paged, bounded; see SEARCH_INDEX.md)
// ---------------------------------------------------------------------------

enum class EntitySet
{
    Assets,
    Datasets,
    Results,
    Runs,
};

struct WorkspaceQuery
{
    EntitySet set = EntitySet::Assets;
    QString text;                 ///< contains-match on name/source (LIKE escaped)
    QString kind;                 ///< asset kind / dataset kind / result semantic type
    QString state;                ///< asset state / result status
    QString sensor;
    QString modality;
    QString crs;
    QString tag;
    QString collectionId;
    QString datasetId;
    QString runId;
    qint64 acquiredFromMs = 0;    ///< 0 = unbounded
    qint64 acquiredToMs = 0;
    qint64 offset = 0;
    qint64 limit = 50;
    QString sortBy;               ///< "updated"|"name"|"acquisition" (default updated)
};

struct FacetCount
{
    QString value;
    qint64 count = 0;
};

struct WorkspacePage
{
    qint64 total = 0;
    QVector<QVariantMap> items;   ///< row-shaped maps (bounded per page)
    QVector<FacetCount> facets;   ///< populated when query asks for one field
    QString facetField;
};

// ---------------------------------------------------------------------------
// Diagnostics / validation vocabulary (task §6, §10)
// ---------------------------------------------------------------------------

enum class DiagnosticKind
{
    MissingFile,
    ChangedContent,
    ChangedMetadata,
    BrokenUri,
    UnsupportedFormat,
    CrsMissing,
    BandCountChanged,
    ModelArtifactMissing,
    CollectionMemberMissing,
    OrphanResult,
    WorkflowReferenceMissing,
    DuplicateAsset,
    StaleCatalog,
    StoreCorruption,
    PathOutsideProject,
    Other,
};

QString diagnosticKindToString( DiagnosticKind kind );
DiagnosticKind diagnosticKindFromString( const QString &text, DiagnosticKind fallback = DiagnosticKind::Other );

/// Machine-readable finding with a concrete repair suggestion.
struct GovernanceDiagnostic
{
    DiagnosticKind kind = DiagnosticKind::Other;
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    QString code;                 ///< stable machine code, e.g. "asset.missing_file"
    QString entityKind;           ///< "asset","result","run","dataset","workspace"
    QString entityId;
    QString message;
    QJsonObject details;
    QString repairSuggestion;     ///< human + agent actionable
};

/// Counted summary for validator outputs.
struct ValidationSummary
{
    int errors = 0;
    int warnings = 0;
    int infos = 0;
    void add( const GovernanceDiagnostic &d )
    {
        switch ( d.severity )
        {
            case DiagnosticSeverity::Error: ++errors; break;
            case DiagnosticSeverity::Warning: ++warnings; break;
            case DiagnosticSeverity::Info: ++infos; break;
        }
    }
};

} // namespace sicnu::workspace
