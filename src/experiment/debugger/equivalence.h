// equivalence.h — declared equivalence profiles + invariant-based references
// (RS14-06, ADR 0174). Slice D.
//
// Alternative-valid-path handling, made explicit: a student step may differ
// from the reference and still be RIGHT, but only when a DECLARED rule says
// so. Every accepted difference names the rule that accepted it — there is
// no implicit "close enough".
//
// Rule kinds (closed set):
//   operator_group   — a declared set of interchangeable operators for one
//                      scientific role (e.g. two contrast stretches).
//   param_tolerance  — numeric parameter keys within a tolerance count as
//                      equal for one operator (differing keys are reported).
//   geometry_keys    — names the geometry-affecting parameter keys of an
//                      operator; parameter differences confined to those
//                      keys classify as geometry/alignment divergence.
//
// Sibling-ORDER differences are deliberately NOT a rule kind: the
// deterministic topological normalization plus content-digest preference in
// the aligner already absorb pure order differences, and genuinely rewired
// dataflow is a scientific difference that must be reported, not blessed.
//
// Invariant references: when no exact reference run applies, a declared
// invariant set ("no classification without a mask step", "OA within
// [0.8, 1]") is evaluated against one snapshot. Invariants never modify
// state; failures are typed findings.
#pragma once

#include "../../data/data_result.h"
#include "first_divergence.h"
#include "run_snapshot.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment::debugger
{

inline constexpr char kEquivalenceSchemaKind[] = "exp.debugger.equivalence.v1";
inline constexpr char kInvariantSchemaKind[] = "exp.debugger.invariants.v1";

class EquivalenceProfile
{
  public:
    enum class RuleKind
    {
        OperatorGroup,
        ParamTolerance,
        GeometryKeys,
    };

    struct Rule
    {
        RuleKind kind = RuleKind::OperatorGroup;
        QString ruleId;
        QStringList operators;   ///< OperatorGroup: the interchangeable set
        QString op;              ///< ParamTolerance / GeometryKeys: the operator
        QStringList keys;        ///< ParamTolerance / GeometryKeys: parameter keys
        double tolerance = 0.0;  ///< ParamTolerance only

        QJsonObject toJson() const;
        static sicnu::data::Result<Rule> fromJson( const QJsonObject &json );
        bool operator==( const Rule & ) const = default;
    };

    EquivalenceProfile() = default;

    QString profileId;
    QVector<Rule> rules;

    /// Id of the operator-group rule accepting a≈b (a != b); empty when none.
    QString operatorGroupRuleId( const QString &a, const QString &b ) const;

    /// Parameter equivalence under tolerance rules. @p differingKeys receives
    /// the keys that genuinely differ even after tolerance (usable for
    /// geometry-key classification). @p acceptedRuleId receives the id of the
    /// tolerance rule that accepted the comparison, when one did.
    bool paramsEquivalent( const QString &op, const QJsonObject &a, const QJsonObject &b,
                           QStringList *differingKeys = nullptr,
                           QString *acceptedRuleId = nullptr ) const;

    /// Geometry-affecting keys declared for @p op (empty when undeclared).
    QStringList geometryKeysFor( const QString &op ) const;

    QJsonObject toJson() const;
    static sicnu::data::Result<EquivalenceProfile> fromJson( const QJsonObject &json );
};

/// One declared invariant over a single run snapshot.
struct Invariant
{
    enum class Kind
    {
        MetricWithin,        ///< numeric metric leaf within [min, max]
        NoStepOfOperator,    ///< no step uses this operator id
        StepCountAtLeast,    ///< at least N recorded steps
        FinalDigestEquals,   ///< last step's output digest equals the value
    };
    Kind kind = Kind::StepCountAtLeast;
    QString invariantId;
    QString metricPath;      ///< MetricWithin
    double minValue = 0.0;
    double maxValue = 0.0;
    QString operatorId;      ///< NoStepOfOperator
    int stepCount = 0;       ///< StepCountAtLeast
    QString digest;          ///< FinalDigestEquals

    QJsonObject toJson() const;
    static sicnu::data::Result<Invariant> fromJson( const QJsonObject &json );
};

struct InvariantCheckResult
{
    QString invariantId;
    bool passed = false;
    /// false when the evidence needed to evaluate the invariant is absent —
    /// an unevaluable invariant is a GAP, never a silent pass.
    bool evaluable = true;
    QString detail;

    QJsonObject toJson() const;
};

/// Evaluates declared invariants against one snapshot. Pure, read-only.
QVector<InvariantCheckResult> evaluateInvariants( const QVector<Invariant> &invariants,
                                                  const RunSnapshot &snapshot );

/// Invariant-reference analysis: no reference run, only declared invariants.
/// The verdict is "divergent" when any evaluable invariant fails,
/// "incomplete" when any invariant was unevaluable (with named gaps), and
/// "equivalent" when all pass (the invariants ARE the reference contract).
sicnu::data::Result<FirstDivergenceReport> analyzeAgainstInvariants(
    const RunSnapshot &student,
    const QVector<Invariant> &invariants,
    const FirstDivergenceOptions &options = {} );

} // namespace sicnu::experiment::debugger
