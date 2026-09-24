// equivalence.cpp — declared equivalence + invariant references
// (RS14-06, ADR 0174). Slice D GREEN implementation.

#include "equivalence.h"

#include <cmath>
#include <limits>
#include <QJsonArray>
#include <QSet>

#include "../metric_path.h"

namespace sicnu::experiment::debugger
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic malformed( const QString &detail )
{
    return { QLatin1String( kCodeMalformedSnapshot ),
             QStringLiteral( "equivalence document rejected: %1" ).arg( detail ),
             DiagnosticSeverity::Error };
}

} // namespace

// --- EquivalenceProfile::Rule ---------------------------------------------------

QJsonObject EquivalenceProfile::Rule::toJson() const
{
    QJsonObject json;
    QString kind;
    switch ( this->kind )
    {
        case RuleKind::OperatorGroup:
            kind = QStringLiteral( "operator_group" );
            break;
        case RuleKind::ParamTolerance:
            kind = QStringLiteral( "param_tolerance" );
            break;
        case RuleKind::GeometryKeys:
            kind = QStringLiteral( "geometry_keys" );
            break;
    }
    json.insert( QLatin1String( "kind" ), kind );
    json.insert( QLatin1String( "rule_id" ), ruleId );
    if ( !operators.isEmpty() )
    {
        QJsonArray ops;
        for ( const QString &op : operators )
            ops.append( op );
        json.insert( QLatin1String( "operators" ), ops );
    }
    if ( !this->op.isEmpty() )
        json.insert( QLatin1String( "operator" ), this->op );
    if ( !keys.isEmpty() )
    {
        QJsonArray keyArray;
        for ( const QString &key : keys )
            keyArray.append( key );
        json.insert( QLatin1String( "keys" ), keyArray );
    }
    if ( this->kind == RuleKind::ParamTolerance )
        json.insert( QLatin1String( "tolerance" ), tolerance );
    return json;
}

Result<EquivalenceProfile::Rule> EquivalenceProfile::Rule::fromJson( const QJsonObject &json )
{
    Rule rule;
    rule.ruleId = json.value( QLatin1String( "rule_id" ) ).toString();
    if ( rule.ruleId.isEmpty() )
        return Result<Rule>::failure( malformed( QStringLiteral( "rule_id is empty" ) ) );

    const QString kind = json.value( QLatin1String( "kind" ) ).toString();
    if ( kind == QLatin1String( "operator_group" ) )
        rule.kind = RuleKind::OperatorGroup;
    else if ( kind == QLatin1String( "param_tolerance" ) )
        rule.kind = RuleKind::ParamTolerance;
    else if ( kind == QLatin1String( "geometry_keys" ) )
        rule.kind = RuleKind::GeometryKeys;
    else
        return Result<Rule>::failure(
            malformed( QStringLiteral( "unknown rule kind '%1'" ).arg( kind ) ) );

    for ( const QJsonValue &value : json.value( QLatin1String( "operators" ) ).toArray() )
        rule.operators << value.toString();
    rule.op = json.value( QLatin1String( "operator" ) ).toString();
    for ( const QJsonValue &value : json.value( QLatin1String( "keys" ) ).toArray() )
        rule.keys << value.toString();
    rule.tolerance = json.value( QLatin1String( "tolerance" ) ).toDouble( 0.0 );

    switch ( rule.kind )
    {
        case RuleKind::OperatorGroup:
            if ( rule.operators.size() < 2 )
                return Result<Rule>::failure( malformed(
                    QStringLiteral( "operator_group rule '%1' declares fewer than two operators" )
                        .arg( rule.ruleId ) ) );
            break;
        case RuleKind::ParamTolerance:
            if ( rule.op.isEmpty() || rule.keys.isEmpty() || rule.tolerance < 0.0 )
                return Result<Rule>::failure( malformed(
                    QStringLiteral( "param_tolerance rule '%1' needs an operator, keys and a non-negative tolerance" )
                        .arg( rule.ruleId ) ) );
            break;
        case RuleKind::GeometryKeys:
            if ( rule.op.isEmpty() || rule.keys.isEmpty() )
                return Result<Rule>::failure( malformed(
                    QStringLiteral( "geometry_keys rule '%1' needs an operator and keys" )
                        .arg( rule.ruleId ) ) );
            break;
    }
    return Result<Rule>::success( rule );
}

// --- EquivalenceProfile -----------------------------------------------------------

QString EquivalenceProfile::operatorGroupRuleId( const QString &a, const QString &b ) const
{
    if ( a.isEmpty() || b.isEmpty() || a == b )
        return {};
    for ( const Rule &rule : rules )
    {
        if ( rule.kind != RuleKind::OperatorGroup )
            continue;
        if ( rule.operators.contains( a ) && rule.operators.contains( b ) )
            return rule.ruleId;
    }
    return {};
}

bool EquivalenceProfile::paramsEquivalent( const QString &op, const QJsonObject &a,
                                           const QJsonObject &b,
                                           QStringList *differingKeys,
                                           QString *acceptedRuleId ) const
{
    if ( differingKeys )
        differingKeys->clear();
    if ( acceptedRuleId )
        acceptedRuleId->clear();
    // Collect every key present on either side; absent-on-one-side counts as
    // a real difference (a missing threshold is not a tolerated threshold).
    QSet<QString> keysUnion;
    for ( auto it = a.begin(); it != a.end(); ++it )
        keysUnion.insert( it.key() );
    for ( auto it = b.begin(); it != b.end(); ++it )
        keysUnion.insert( it.key() );

    bool allEquivalent = true;
    for ( const QString &key : keysUnion )
    {
        const QJsonValue va = a.value( key );
        const QJsonValue vb = b.value( key );
        if ( va == vb )
            continue;
        const double da = va.toDouble( std::numeric_limits<double>::quiet_NaN() );
        const double db = vb.toDouble( std::numeric_limits<double>::quiet_NaN() );
        bool tolerated = false;
        for ( const Rule &rule : rules )
        {
            if ( rule.kind != RuleKind::ParamTolerance || rule.op != op )
                continue;
            if ( !rule.keys.contains( key ) )
                continue;
            if ( std::isnan( da ) || std::isnan( db ) )
                break; // not two numbers — tolerance cannot apply
            if ( std::abs( da - db ) <= rule.tolerance )
            {
                tolerated = true;
                if ( acceptedRuleId )
                    *acceptedRuleId = rule.ruleId;
                break;
            }
        }
        if ( !tolerated )
        {
            allEquivalent = false;
            if ( differingKeys )
                differingKeys->append( key );
        }
    }
    return allEquivalent;
}

QStringList EquivalenceProfile::geometryKeysFor( const QString &op ) const
{
    for ( const Rule &rule : rules )
        if ( rule.kind == RuleKind::GeometryKeys && rule.op == op )
            return rule.keys;
    return {};
}

QJsonObject EquivalenceProfile::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "kind" ), QLatin1String( kEquivalenceSchemaKind ) );
    json.insert( QLatin1String( "schema_version" ), kDebuggerSchemaVersion );
    json.insert( QLatin1String( "profile_id" ), profileId );
    QJsonArray rulesJson;
    for ( const Rule &rule : rules )
        rulesJson.append( rule.toJson() );
    json.insert( QLatin1String( "rules" ), rulesJson );
    return json;
}

Result<EquivalenceProfile> EquivalenceProfile::fromJson( const QJsonObject &json )
{
    if ( json.value( QLatin1String( "kind" ) ).toString() != QLatin1String( kEquivalenceSchemaKind ) )
        return Result<EquivalenceProfile>::failure(
            malformed( QStringLiteral( "unsupported kind" ) ) );
    if ( json.value( QLatin1String( "schema_version" ) ).toInt() != kDebuggerSchemaVersion )
        return Result<EquivalenceProfile>::failure(
            malformed( QStringLiteral( "unsupported schema version" ) ) );

    EquivalenceProfile profile;
    profile.profileId = json.value( QLatin1String( "profile_id" ) ).toString();
    QSet<QString> ruleIds;
    for ( const QJsonValue &value : json.value( QLatin1String( "rules" ) ).toArray() )
    {
        auto rule = Rule::fromJson( value.toObject() );
        if ( !rule )
            return Result<EquivalenceProfile>::failure( rule.diagnostics() );
        if ( ruleIds.contains( rule->ruleId ) )
            return Result<EquivalenceProfile>::failure(
                malformed( QStringLiteral( "duplicate rule id '%1'" ).arg( rule->ruleId ) ) );
        ruleIds.insert( rule->ruleId );
        profile.rules.append( rule.take() );
    }
    return Result<EquivalenceProfile>::success( profile );
}

// --- Invariants -----------------------------------------------------------------

QJsonObject Invariant::toJson() const
{
    QJsonObject json;
    QString kind;
    switch ( this->kind )
    {
        case Kind::MetricWithin:
            kind = QStringLiteral( "metric_within" );
            break;
        case Kind::NoStepOfOperator:
            kind = QStringLiteral( "no_step_of_operator" );
            break;
        case Kind::StepCountAtLeast:
            kind = QStringLiteral( "step_count_at_least" );
            break;
        case Kind::FinalDigestEquals:
            kind = QStringLiteral( "final_digest_equals" );
            break;
    }
    json.insert( QLatin1String( "kind" ), kind );
    json.insert( QLatin1String( "invariant_id" ), invariantId );
    if ( !metricPath.isEmpty() )
    {
        json.insert( QLatin1String( "metric_path" ), metricPath );
        json.insert( QLatin1String( "min" ), minValue );
        json.insert( QLatin1String( "max" ), maxValue );
    }
    if ( !operatorId.isEmpty() )
        json.insert( QLatin1String( "operator" ), operatorId );
    if ( this->kind == Kind::StepCountAtLeast )
        json.insert( QLatin1String( "step_count" ), stepCount );
    if ( !digest.isEmpty() )
        json.insert( QLatin1String( "digest" ), digest );
    return json;
}

Result<Invariant> Invariant::fromJson( const QJsonObject &json )
{
    Invariant invariant;
    invariant.invariantId = json.value( QLatin1String( "invariant_id" ) ).toString();
    if ( invariant.invariantId.isEmpty() )
        return Result<Invariant>::failure( malformed( QStringLiteral( "invariant_id is empty" ) ) );
    const QString kind = json.value( QLatin1String( "kind" ) ).toString();
    if ( kind == QLatin1String( "metric_within" ) )
        invariant.kind = Kind::MetricWithin;
    else if ( kind == QLatin1String( "no_step_of_operator" ) )
        invariant.kind = Kind::NoStepOfOperator;
    else if ( kind == QLatin1String( "step_count_at_least" ) )
        invariant.kind = Kind::StepCountAtLeast;
    else if ( kind == QLatin1String( "final_digest_equals" ) )
        invariant.kind = Kind::FinalDigestEquals;
    else
        return Result<Invariant>::failure(
            malformed( QStringLiteral( "unknown invariant kind '%1'" ).arg( kind ) ) );

    invariant.metricPath = json.value( QLatin1String( "metric_path" ) ).toString();
    invariant.minValue = json.value( QLatin1String( "min" ) ).toDouble( 0.0 );
    invariant.maxValue = json.value( QLatin1String( "max" ) ).toDouble( 0.0 );
    invariant.operatorId = json.value( QLatin1String( "operator" ) ).toString();
    invariant.stepCount = json.value( QLatin1String( "step_count" ) ).toInt( 0 );
    invariant.digest = json.value( QLatin1String( "digest" ) ).toString();

    switch ( invariant.kind )
    {
        case Kind::MetricWithin:
            if ( invariant.metricPath.isEmpty() || invariant.maxValue < invariant.minValue )
                return Result<Invariant>::failure( malformed(
                    QStringLiteral( "metric_within invariant '%1' needs a path and min ≤ max" )
                        .arg( invariant.invariantId ) ) );
            break;
        case Kind::NoStepOfOperator:
            if ( invariant.operatorId.isEmpty() )
                return Result<Invariant>::failure( malformed(
                    QStringLiteral( "no_step_of_operator invariant '%1' needs an operator" )
                        .arg( invariant.invariantId ) ) );
            break;
        case Kind::StepCountAtLeast:
            if ( invariant.stepCount < 0 )
                return Result<Invariant>::failure( malformed(
                    QStringLiteral( "step_count_at_least invariant '%1' needs a non-negative count" )
                        .arg( invariant.invariantId ) ) );
            break;
        case Kind::FinalDigestEquals:
            if ( invariant.digest.isEmpty() )
                return Result<Invariant>::failure( malformed(
                    QStringLiteral( "final_digest_equals invariant '%1' needs a digest" )
                        .arg( invariant.invariantId ) ) );
            break;
    }
    return Result<Invariant>::success( invariant );
}

QJsonObject InvariantCheckResult::toJson() const
{
    QJsonObject json;
    json.insert( QLatin1String( "invariant_id" ), invariantId );
    json.insert( QLatin1String( "passed" ), passed );
    json.insert( QLatin1String( "evaluable" ), evaluable );
    if ( !detail.isEmpty() )
        json.insert( QLatin1String( "detail" ), detail );
    return json;
}

QVector<InvariantCheckResult> evaluateInvariants( const QVector<Invariant> &invariants,
                                                  const RunSnapshot &snapshot )
{
    QVector<InvariantCheckResult> results;
    for ( const Invariant &invariant : invariants )
    {
        InvariantCheckResult check;
        check.invariantId = invariant.invariantId;
        switch ( invariant.kind )
        {
            case Invariant::Kind::MetricWithin:
            {
                // Metric leaf lookup is the platform's existing single truth.
                const auto value = sicnu::experiment::metricValueAtPath(
                    snapshot.metrics(), invariant.metricPath );
                if ( !value.has_value() )
                {
                    check.evaluable = false;
                    check.detail = QStringLiteral( "metric '%1' not present in the run metrics" )
                                       .arg( invariant.metricPath );
                }
                else
                {
                    check.passed = *value >= invariant.minValue && *value <= invariant.maxValue;
                    check.detail = QStringLiteral( "metric '%1' = %2 within [%3, %4]" )
                                       .arg( invariant.metricPath )
                                       .arg( *value )
                                       .arg( invariant.minValue )
                                       .arg( invariant.maxValue );
                }
                break;
            }
            case Invariant::Kind::NoStepOfOperator:
            {
                bool found = false;
                for ( const StepSnapshot &step : snapshot.steps() )
                    if ( step.operatorId == invariant.operatorId )
                    {
                        found = true;
                        break;
                    }
                check.passed = !found;
                check.detail = found
                    ? QStringLiteral( "operator '%1' used despite prohibition" ).arg( invariant.operatorId )
                    : QStringLiteral( "operator '%1' absent as required" ).arg( invariant.operatorId );
                break;
            }
            case Invariant::Kind::StepCountAtLeast:
                check.passed = snapshot.steps().size() >= invariant.stepCount;
                check.detail = QStringLiteral( "%1 recorded steps (requires at least %2)" )
                                   .arg( snapshot.steps().size() )
                                   .arg( invariant.stepCount );
                break;
            case Invariant::Kind::FinalDigestEquals:
                if ( snapshot.steps().isEmpty() )
                {
                    check.evaluable = false;
                    check.detail = QStringLiteral( "no steps recorded — nothing to compare the final digest against" );
                }
                else
                {
                    // "Final" must mean a SINK of the recorded pipeline, not
                    // "whatever happens to sort last": steps are stored in
                    // (topological, stepId) order, so a renamed multi-branch
                    // pipeline could pin this check onto a non-terminal step
                    // and flip pass/fail without any execution change. One
                    // sink decides; several sinks are a named ambiguity.
                    QSet<QString> dependedOn;
                    for ( const StepSnapshot &step : snapshot.steps() )
                        for ( const QString &dependency : step.dependencies )
                            dependedOn.insert( dependency );
                    QList<const StepSnapshot *> sinks;
                    for ( const StepSnapshot &step : snapshot.steps() )
                        if ( !dependedOn.contains( step.stepId ) )
                            sinks.append( &step );
                    if ( sinks.size() != 1 )
                    {
                        check.evaluable = false;
                        check.passed = false;
                        check.detail =
                            QStringLiteral( "pipeline ends with %1 sink steps;"
                                            " the final digest is undefined"
                                            " without naming one" )
                                .arg( sinks.size() );
                    }
                    else
                    {
                        const StepSnapshot *finalStep = sinks.first();
                        check.passed = finalStep->outputDigest == invariant.digest;
                        check.detail = check.passed
                            ? QStringLiteral( "final output digest matches" )
                            : QStringLiteral( "final output digest %1 != declared %2" )
                                  .arg( finalStep->outputDigest.isEmpty()
                                            ? QStringLiteral( "<absent>" )
                                            : finalStep->outputDigest,
                                        invariant.digest );
                        if ( finalStep->outputDigest.isEmpty() )
                        {
                            check.evaluable = false;
                            check.detail = QStringLiteral( "final step carries no output digest" );
                            check.passed = false;
                        }
                    }
                }
                break;
        }
        results.append( check );
    }
    return results;
}

Result<FirstDivergenceReport> analyzeAgainstInvariants( const RunSnapshot &student,
                                                        const QVector<Invariant> &invariants,
                                                        const FirstDivergenceOptions &options )
{
    FirstDivergenceReport report;
    report.studentRunId = student.runId();

    const QVector<InvariantCheckResult> checks = evaluateInvariants( invariants, student );
    bool anyFailed = false;
    bool anyUnevaluable = false;
    for ( const InvariantCheckResult &check : checks )
    {
        if ( !check.evaluable )
        {
            anyUnevaluable = true;
            report.evidenceGaps << QStringLiteral( "invariant '%1' unevaluable: %2" )
                                       .arg( check.invariantId, check.detail );
            continue;
        }
        if ( check.passed )
            continue;
        // Seeing the failure is a verdict input, not a truncation decision:
        // set anyFailed BEFORE the cap check, or maxFindings == 0 takes the
        // truncation branch on the first failure and the report claims
        // "equivalent" while its own checks record passed:false.
        anyFailed = true;
        if ( report.additionalFindings.size() >= options.maxFindings )
        {
            report.evidenceGaps
                << QStringLiteral( "further invariant failures truncated at %1 findings" )
                       .arg( options.maxFindings );
            break;
        }
        DivergenceFinding finding;
        // A failed invariant is a DECLARED-CONTRACT divergence: there is no
        // process reference to compare against, so the taxonomy's
        // unknown/non-comparable kind is the honest carrier — the finding's
        // evidence names the exact invariant and failure.
        finding.kind = DivergenceKind::UnknownNonComparable;
        finding.studentStepId = student.steps().isEmpty()
                                   ? QString()
                                   : student.steps().constLast().stepId;
        finding.confidence = CausalConfidence::Medium;
        finding.evidence << QStringLiteral( "invariant '%1' failed: %2" )
                                .arg( check.invariantId, check.detail );
        report.additionalFindings.append( finding );
    }

    if ( anyFailed )
        report.verdict = QStringLiteral( "divergent" );
    else if ( anyUnevaluable )
        report.verdict = QStringLiteral( "incomplete" );
    else
        report.verdict = QStringLiteral( "equivalent" );
    report.hasFirstDivergence = anyFailed;
    if ( anyFailed && !report.additionalFindings.isEmpty() )
        report.firstDivergence = report.additionalFindings.front();

    // Machine-checkable invariant ledger rides in the alignment slot (this
    // report form has no alignment); the ledger carries its own schema kind.
    QJsonObject invariantsJson;
    invariantsJson.insert( QLatin1String( "kind" ), QLatin1String( kInvariantSchemaKind ) );
    invariantsJson.insert( QLatin1String( "schema_version" ), kDebuggerSchemaVersion );
    QJsonArray checksJson;
    for ( const InvariantCheckResult &check : checks )
        checksJson.append( check.toJson() );
    invariantsJson.insert( QLatin1String( "checks" ), checksJson );
    report.alignment = invariantsJson;

    return Result<FirstDivergenceReport>::success( report );
}

} // namespace sicnu::experiment::debugger
