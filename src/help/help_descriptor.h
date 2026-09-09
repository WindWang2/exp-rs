/***************************************************************************
 * help_descriptor.h — knowledge record types for the Unified Help System 6.0
 *
 * HelpDescriptor is the registry's unit: a bounded, versioned core plus
 * kind-specific payloads composed by value. Facts owned by authoritative
 * sources (schema ranges/defaults, shortcuts, availability rules, error
 * categories) are NEVER stored here — they are derived at projection time by
 * the providers. Descriptors carry only additive human/scientific knowledge.
 *
 * All user-facing text is Chinese-first; ids/keywords may be bilingual.
 ***************************************************************************/
#pragma once

#include "data/data_result.h"
#include "help/help_id.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace sicnu::help
{

/// Severity vocabulary reuses the shared data layer (sicnu::data), so help
/// diagnostics speak the same language as dataset/asset diagnostics.
/// (sicnu::data::DiagnosticSeverity: Info | Warning | Error.)

/// Whether retrying makes sense for a diagnostic. Kept in the help layer's own
/// vocabulary but drift-checked against HarnessError's RetryClass.
enum class RetrySense
{
    None,      ///< deterministic failure; retrying the same inputs cannot help
    Manual,    ///< only an explicit user/agent decision may retry
    Transient, ///< safe to auto-retry, bounded
    Derived,   ///< defer to the origin taxonomy's retry class at query time
};

inline const char *retrySenseName( RetrySense sense )
{
    switch ( sense ) {
    case RetrySense::None:
        return "none";
    case RetrySense::Manual:
        return "manual";
    case RetrySense::Transient:
        return "transient";
    case RetrySense::Derived:
        return "derived";
    }
    return "derived";
}

/// Command-specific knowledge (command.* descriptors). Registry-derived facts
/// (title, shortcut, category, availability) are NOT duplicated here.
struct CommandHelp
{
    /// Why this command exists — the goal a user achieves with it (中文).
    QString purpose;
    /// What must hold before invoking (中文, user-checkable statements).
    QStringList prerequisites;
    /// The natural follow-up (中文), e.g. "再次执行可结束编辑会话".
    QString suggestedNextAction;
};

/// Additive scientific knowledge for one operator parameter
/// (parameter.<operator>.<param> descriptors). Machine facts (type, range,
/// default, enum, required) always come from the operator schema.
struct ParameterKnowledge
{
    /// Physical/UI unit (中文, e.g. "像素", "米", "dB", "百分比").
    QString unit;
    /// What the parameter controls scientifically (中文).
    QString meaning;
    /// Recommended value/range when knowledge exists (中文).
    QString recommended;
    /// The cost/benefit trade-off of larger/smaller or on/off (中文).
    QString tradeOff;
    /// Performance/memory impact note (中文).
    QString performanceNote;
    /// Pitfalls expressed as user warnings (中文).
    QStringList warnings;
    /// Other parameter names this one depends on (schema param names).
    QStringList dependsOn;
    /// True when the knowledge is curated (deep tier) vs schema-only.
    bool curated = true;
};

/// Algorithm/concept page (operator.* and concept.* descriptors): the ten-part
/// structure the goal requires. Kept concise; narrative depth lives in linked
/// docs, not here.
struct AlgorithmPage
{
    QString whatItDoes;      ///< 一句话原理
    QString whenToUse;       ///< 适用场景
    QStringList inputs;      ///< 输入要求（含单位/域）
    QStringList outputs;     ///< 输出含义
    QStringList assumptions; ///< 科学假设（如数据已定标）
    QString unitsDomain;     ///< 数值域/单位约定
    QStringList keyParameters; ///< 关键参数名（schema 参数名）
    QStringList limitations;   ///< 明确的局限
    QStringList failureModes;  ///< 典型失败模式（链接 diagnostic ids 更佳）
    QStringList relatedTools;  ///< 相关 operator ids / command ids
};

/// Workbench / empty-state guidance (workbench.* descriptors): small,
/// actionable, non-modal.
struct GuidanceDescriptor
{
    /// Headline shown in the empty state (中文).
    QString headline;
    /// 1–3 sentence body (中文).
    QString body;
    /// Commands that move the user forward (command ids, in suggested order).
    QStringList actionCommandIds;
    /// Deeper help topic (usually a concept.* or operator.* id).
    QString helpTopicId;
};

/// Diagnostic knowledge (diagnostic.* descriptors). The original machine code
/// is preserved verbatim in originCode — the catalog never erases it.
struct DiagnosticInfo
{
    QString originFamily; ///< "harness" | "operator" | "geospatial" | "dataset" | "preflight" | "rs"
    QString originCode;   ///< byte-identical machine code (e.g. "DATASET_NOT_FOUND")
    QString whatHappened; ///< 中文
    QString whyItMatters; ///< 中文，用遥感/GIS 后果表述
    sicnu::data::DiagnosticSeverity severity = sicnu::data::DiagnosticSeverity::Error;
    RetrySense retrySense = RetrySense::Derived;
    QStringList remediation;       ///< 中文，第一条为最廉价的安全修复
    QString technicalNote;         ///< 可选，面向高级用户/agent
};

/// Core record. Payloads are optional and kind-appropriate; the registry does
/// not interpret payload contents (presentation layers do).
struct HelpDescriptor
{
    QString id;                    ///< validated Help ID
    HelpKind kind = HelpKind::Concept;
    QString title;                 ///< 中文标题（工具提示第一行）
    QString summary;               ///< ≤120 字摘要（工具提示/搜索结果正文）
    QString category;              ///< Help Center 分区（见 help_center IA）
    QStringList keywords;          ///< 检索词（中/EN）
    QStringList relatedIds;        ///< related topic ids（registry 校验存在性）
    QStringList diagnosticIds;     ///< related diagnostic.* ids（校验存在性）
    QStringList docRefs;           ///< 仓库相对文档路径（如 docs/processing/sar-domain.md）
    bool deprecated = false;
    QString supersededBy;          ///< deprecated 时的目标 id（别名，一跳）

    std::optional<CommandHelp> command;
    std::optional<ParameterKnowledge> parameter;
    std::optional<AlgorithmPage> algorithm;
    std::optional<GuidanceDescriptor> guidance;
    std::optional<DiagnosticInfo> diagnostic;
};

/// Version of the descriptor contract; bumped on breaking field changes.
inline constexpr int kHelpDescriptorVersion = 1;

} // namespace sicnu::help
