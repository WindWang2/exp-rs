#include "experiment_studio/study_designer.h"

#include "experiment/experiment_matrix.h"

#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace sicnu::experiment_studio
{

using sicnu::study::ParameterDimension;
using sicnu::study::ParameterStudySpec;
using sicnu::study::SamplingStrategy;
using sicnu::study::StudyBudget;
using sicnu::study::StudyMetricSpec;
using sicnu::study::sampleStudyPoints;
using sicnu::experiment::kMaxMatrixCells;


namespace
{

bool isNumericType( const QString &type )
{
    return type == QStringLiteral( "number" ) || type == QStringLiteral( "integer" )
           || type == QStringLiteral( "Number" ) || type == QStringLiteral( "Integer" );
}

DesignerParameterOption fromProperty( const QString &name, const QJsonObject &prop )
{
    DesignerParameterOption opt;
    opt.parameterPath = name;
    opt.description = prop.value( QStringLiteral( "description" ) ).toString();
    const QString type = prop.value( QStringLiteral( "type" ) ).toString();
    if ( prop.contains( QStringLiteral( "enum" ) ) )
    {
        opt.sweepable = false;
        opt.refuseReason = QStringLiteral( "experiment_studio.param_not_numeric: enum not sweepable in v1" );
        return opt;
    }
    if ( !isNumericType( type ) && !prop.contains( QStringLiteral( "minimum" ) )
         && !prop.contains( QStringLiteral( "maximum" ) ) )
    {
        // Heuristic: raster/string/boolean/output
        if ( type == QStringLiteral( "boolean" ) || type == QStringLiteral( "string" )
             || type.isEmpty() || name == QStringLiteral( "input" )
             || name == QStringLiteral( "output" ) || name.endsWith( QStringLiteral( "Path" ) )
             || name.endsWith( QStringLiteral( "_path" ) ) )
        {
            opt.sweepable = false;
            opt.refuseReason =
                QStringLiteral( "experiment_studio.param_not_sweepable: %1 type=%2" ).arg( name, type );
            return opt;
        }
    }
    if ( !isNumericType( type ) && type != QStringLiteral( "number" ) && type != QStringLiteral( "integer" )
         && !prop.contains( QStringLiteral( "minimum" ) ) )
    {
        opt.sweepable = false;
        opt.refuseReason =
            QStringLiteral( "experiment_studio.param_not_numeric: %1" ).arg( name );
        return opt;
    }
    opt.sweepable = true;
    if ( prop.contains( QStringLiteral( "minimum" ) ) || prop.contains( QStringLiteral( "maximum" ) ) )
    {
        opt.hasRange = true;
        opt.schemaMin = prop.value( QStringLiteral( "minimum" ) ).toDouble();
        opt.schemaMax = prop.value( QStringLiteral( "maximum" ) ).toDouble( opt.schemaMin + 1.0 );
    }
    opt.defaultValue = prop.value( QStringLiteral( "default" ) ).toDouble();
    return opt;
}

} // namespace

QJsonObject DesignerParameterOption::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "parameter_path" ), parameterPath );
    o.insert( QStringLiteral( "description" ), description );
    o.insert( QStringLiteral( "schema_min" ), schemaMin );
    o.insert( QStringLiteral( "schema_max" ), schemaMax );
    o.insert( QStringLiteral( "has_range" ), hasRange );
    o.insert( QStringLiteral( "default_value" ), defaultValue );
    o.insert( QStringLiteral( "sweepable" ), sweepable );
    o.insert( QStringLiteral( "refuse_reason" ), refuseReason );
    return o;
}

QJsonObject StudyDesignerViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    o.insert( QStringLiteral( "determinism_grade" ), determinismGrade );
    o.insert( QStringLiteral( "operator_suitable" ), operatorSuitable );
    QJsonArray issues;
    for ( const QString &s : suitabilityIssues )
        issues.append( s );
    o.insert( QStringLiteral( "suitability_issues" ), issues );
    QJsonArray params;
    for ( const auto &p : parameters )
        params.append( p.toJson() );
    o.insert( QStringLiteral( "parameters" ), params );
    o.insert( QStringLiteral( "draft_spec" ), draftSpec.toJson() );
    o.insert( QStringLiteral( "estimated_points" ), static_cast<double>( estimatedPoints ) );
    o.insert( QStringLiteral( "draft_valid" ), draftValid );
    QJsonArray draftIss;
    for ( const QString &s : draftIssues )
        draftIss.append( s );
    o.insert( QStringLiteral( "draft_issues" ), draftIss );
    QJsonObject pol;
    pol.insert( QStringLiteral( "max_points" ), static_cast<double>( policy.maxPoints ) );
    pol.insert( QStringLiteral( "max_in_flight" ), policy.maxInFlight );
    pol.insert( QStringLiteral( "max_disk_bytes" ), static_cast<double>( policy.maxDiskBytes ) );
    pol.insert( QStringLiteral( "max_per_run_deadline_ms" ),
                static_cast<double>( policy.maxPerRunDeadlineMs ) );
    o.insert( QStringLiteral( "policy" ), pol );
    return o;
}

Result<QVector<DesignerParameterOption>> projectOperatorParameters( const QJsonObject &operatorSchema )
{
    QVector<DesignerParameterOption> out;
    // Shape A: JSON Schema properties map
    if ( operatorSchema.contains( QStringLiteral( "properties" ) )
         && operatorSchema.value( QStringLiteral( "properties" ) ).isObject() )
    {
        const QJsonObject props = operatorSchema.value( QStringLiteral( "properties" ) ).toObject();
        for ( auto it = props.begin(); it != props.end(); ++it )
        {
            if ( !it.value().isObject() )
                continue;
            out.append( fromProperty( it.key(), it.value().toObject() ) );
        }
        return Result<QVector<DesignerParameterOption>>::success( out );
    }
    // Shape B: params array of {name, type, ...}
    if ( operatorSchema.contains( QStringLiteral( "params" ) )
         && operatorSchema.value( QStringLiteral( "params" ) ).isArray() )
    {
        const QJsonArray arr = operatorSchema.value( QStringLiteral( "params" ) ).toArray();
        for ( const QJsonValue &v : arr )
        {
            if ( !v.isObject() )
                continue;
            const QJsonObject p = v.toObject();
            const QString name = p.value( QStringLiteral( "name" ) ).toString();
            if ( name.isEmpty() )
                continue;
            out.append( fromProperty( name, p ) );
        }
        return Result<QVector<DesignerParameterOption>>::success( out );
    }
    // Shape C: flat {paramName: {type:...}} without properties wrapper
    for ( auto it = operatorSchema.begin(); it != operatorSchema.end(); ++it )
    {
        if ( !it.value().isObject() )
            continue;
        const QJsonObject p = it.value().toObject();
        if ( !p.contains( QStringLiteral( "type" ) ) && !p.contains( QStringLiteral( "minimum" ) ) )
            continue;
        out.append( fromProperty( it.key(), p ) );
    }
    if ( out.isEmpty() )
        return Result<QVector<DesignerParameterOption>>::failure(
            studioError( QStringLiteral( "experiment_studio.schema_empty" ),
                         QStringLiteral( "operator schema has no projectable parameters" ) ) );
    return Result<QVector<DesignerParameterOption>>::success( out );
}

Result<qint64> estimateStudyPointCount( const ParameterStudySpec &spec )
{
    const auto valid = spec.validate();
    if ( !valid )
        return Result<qint64>::failure( valid.diagnostics() );
    const auto points = sampleStudyPoints( spec );
    if ( !points )
        return Result<qint64>::failure( points.diagnostics() );
    return Result<qint64>::success( static_cast<qint64>( points.value().size() ) );
}

Result<StudyDesignerViewModel> buildStudyDesignerViewModel(
    const QString &algorithmId,
    const QJsonObject &operatorSchema,
    const QJsonObject &baseParameters,
    SamplingStrategy strategy,
    const QVector<ParameterDimension> &dimensions,
    const StudyBudget &budget,
    const QStringList &metricNames,
    const QString &studyId,
    const QString &experimentId,
    const StudioResourcePolicy &policy,
    const QString &determinismGrade,
    bool spatialComparison,
    const QString &objectiveMetric,
    const QVector<StudyMetricSpec> &objectiveMetrics )
{
    StudyDesignerViewModel vm;
    vm.algorithmId = algorithmId;
    vm.determinismGrade = determinismGrade;
    vm.policy = policy;

    if ( algorithmId.isEmpty() )
    {
        vm.operatorSuitable = false;
        vm.suitabilityIssues.append(
            QStringLiteral( "experiment_studio.missing_algorithm: algorithm_id required" ) );
    }
    if ( determinismGrade == QStringLiteral( "unknown" ) )
    {
        vm.suitabilityIssues.append(
            QStringLiteral( "experiment_studio.determinism_unknown: prefer bit_exact/tolerance operators" ) );
    }

    auto params = projectOperatorParameters( operatorSchema );
    if ( params )
        vm.parameters = params.value();
    else
    {
        for ( const auto &d : params.diagnostics() )
            vm.suitabilityIssues.append( d.code + QLatin1Char( ':' ) + d.message );
    }

    // Guard: dimensions must reference sweepable params when schema known
    QSet<QString> sweepable;
    for ( const auto &p : vm.parameters )
        if ( p.sweepable )
            sweepable.insert( p.parameterPath );
    for ( const auto &dim : dimensions )
    {
        if ( !vm.parameters.isEmpty() && !sweepable.contains( dim.parameterPath ) )
        {
            vm.draftIssues.append(
                QStringLiteral( "experiment_studio.unsuitable_dimension:%1" ).arg( dim.parameterPath ) );
        }
        if ( !vm.parameters.isEmpty() )
        {
            for ( const auto &p : vm.parameters )
            {
                if ( p.parameterPath == dim.parameterPath && p.hasRange )
                {
                    if ( dim.minValue < p.schemaMin || dim.maxValue > p.schemaMax )
                    {
                        vm.draftIssues.append(
                            QStringLiteral( "experiment_studio.illegal_range:%1 outside schema [%2,%3]" )
                                .arg( dim.parameterPath )
                                .arg( p.schemaMin )
                                .arg( p.schemaMax ) );
                    }
                }
            }
        }
    }

    ParameterStudySpec spec;
    spec.studyId = studyId;
    spec.experimentId = experimentId;
    spec.algorithmId = algorithmId;
    spec.baseParameters = baseParameters;
    spec.strategy = strategy;
    spec.dimensions = dimensions;
    spec.budget = budget;
    spec.metricNames = metricNames;
    spec.objectiveMetric = objectiveMetric;
    spec.objectiveMetrics = objectiveMetrics;
    spec.spatialComparison = spatialComparison;
    vm.draftSpec = spec;

    // Studio policy pre-check (before core): in-flight / deadline / max points admit
    if ( budget.maxInFlight > policy.maxInFlight )
    {
        vm.draftIssues.append(
            QStringLiteral( "experiment_studio.policy_max_in_flight:%1 > %2" )
                .arg( budget.maxInFlight )
                .arg( policy.maxInFlight ) );
    }
    if ( budget.perRunTimeoutMs > policy.maxPerRunDeadlineMs )
    {
        vm.draftIssues.append(
            QStringLiteral( "experiment_studio.policy_deadline:%1 > %2" )
                .arg( budget.perRunTimeoutMs )
                .arg( policy.maxPerRunDeadlineMs ) );
    }
    if ( budget.maxRuns > policy.maxPoints )
    {
        vm.draftIssues.append(
            QStringLiteral( "experiment_studio.policy_max_points:%1 > %2" )
                .arg( budget.maxRuns )
                .arg( policy.maxPoints ) );
    }

    const auto valid = spec.validate();
    if ( !valid )
    {
        vm.draftValid = false;
        for ( const auto &d : valid.diagnostics() )
            vm.draftIssues.append( d.code + QLatin1Char( ':' ) + d.message );
        // combinatorial estimate for messaging (honest: not truncated)
        qint64 combos = 1;
        for ( const auto &dim : dimensions )
        {
            if ( dim.stepCount > 0 )
            {
                if ( combos > kMaxMatrixCells / dim.stepCount )
                {
                    combos = kMaxMatrixCells + 1;
                    break;
                }
                combos *= dim.stepCount;
            }
        }
        if ( strategy == SamplingStrategy::OneAtATime && !dimensions.isEmpty() )
        {
            combos = 1;
            for ( const auto &dim : dimensions )
                combos += static_cast<qint64>( dim.stepCount ) - 1;
        }
        combos *= std::max<qint64>( 1, budget.seedReplicates );
        vm.estimatedPoints = combos;
        if ( combos > budget.maxRuns || combos > policy.maxPoints )
        {
            vm.draftIssues.append(
                QStringLiteral( "experiment_studio.combo_explosion: estimated %1 points" ).arg( combos ) );
        }
        return Result<StudyDesignerViewModel>::success( vm );
    }

    const auto points = sampleStudyPoints( spec );
    if ( !points )
    {
        vm.draftValid = false;
        for ( const auto &d : points.diagnostics() )
            vm.draftIssues.append( d.code + QLatin1Char( ':' ) + d.message );
        // still surface combo explosion messaging
        for ( const auto &d : points.diagnostics() )
        {
            if ( d.code.contains( QStringLiteral( "budget" ) ) )
                vm.draftIssues.append(
                    QStringLiteral( "experiment_studio.combo_explosion:%1" ).arg( d.message ) );
        }
        return Result<StudyDesignerViewModel>::success( vm );
    }

    vm.estimatedPoints = static_cast<qint64>( points.value().size() );
    if ( vm.estimatedPoints > policy.maxPoints )
    {
        vm.draftValid = false;
        vm.draftIssues.append(
            QStringLiteral( "experiment_studio.policy_max_points: estimated %1 > %2" )
                .arg( vm.estimatedPoints )
                .arg( policy.maxPoints ) );
        return Result<StudyDesignerViewModel>::success( vm );
    }

    vm.draftValid = vm.draftIssues.isEmpty() && vm.operatorSuitable;
    if ( !vm.suitabilityIssues.isEmpty()
         && determinismGrade != QStringLiteral( "bit_exact" )
         && determinismGrade != QStringLiteral( "tolerance" ) )
    {
        // soft warning only — still allow draft if core validates
        vm.operatorSuitable = true;
    }
    if ( vm.draftIssues.isEmpty() )
        vm.draftValid = true;
    else
        vm.draftValid = false;

    return Result<StudyDesignerViewModel>::success( vm );
}

} // namespace sicnu::experiment_studio
