// study_spec.cpp — ParameterStudySpec serialization + typed validation.
#include "study/study_spec.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace sicnu::study
{
namespace
{

Diagnostic specError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

} // namespace

Diagnostic studyError( const QString &code, const QString &message )
{
    return specError( code, message );
}

QString samplingStrategyToString( SamplingStrategy strategy )
{
    switch ( strategy )
    {
        case SamplingStrategy::Grid:
            return QStringLiteral( "grid" );
        case SamplingStrategy::OneAtATime:
            return QStringLiteral( "oat" );
        case SamplingStrategy::LatinHypercube:
            return QStringLiteral( "lhs" );
    }
    return QStringLiteral( "grid" );
}

std::optional<SamplingStrategy> samplingStrategyFromString( const QString &text )
{
    if ( text == QStringLiteral( "grid" ) )
        return SamplingStrategy::Grid;
    if ( text == QStringLiteral( "oat" ) )
        return SamplingStrategy::OneAtATime;
    if ( text == QStringLiteral( "lhs" ) )
        return SamplingStrategy::LatinHypercube;
    return std::nullopt;
}

Result<void> ParameterStudySpec::validate() const
{
    if ( studyId.isEmpty() )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_invalid_study_id" ),
                       QStringLiteral( "study requires a study id" ) ) );
    if ( experimentId.isEmpty() )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_invalid_experiment_id" ),
                       QStringLiteral( "study requires the target experiment id" ) ) );
    if ( algorithmId.isEmpty() )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_invalid_algorithm_id" ),
                       QStringLiteral( "study requires the submitted algorithm id" ) ) );
    if ( baseParameters.contains( QStringLiteral( "output" ) ) )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_output_reserved" ),
                       QStringLiteral( "base_parameters must not set \"output\" — the runner "
                                        "assigns one stable output per point" ) ) );
    if ( dimensions.isEmpty() )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_no_dimensions" ),
                       QStringLiteral( "a sensitivity study requires at least one parameter dimension" ) ) );

    QSet<QString> paths;
    for ( const ParameterDimension &dim : dimensions )
    {
        if ( dim.parameterPath.isEmpty() )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_invalid_dimension_path" ),
                           QStringLiteral( "every dimension requires a parameter path" ) ) );
        if ( !std::isfinite( dim.minValue ) || !std::isfinite( dim.maxValue )
             || !( dim.minValue < dim.maxValue ) )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_invalid_dimension_range" ),
                           QStringLiteral( "dimension %1 requires min < max (finite)" )
                               .arg( dim.parameterPath ) ) );
        if ( dim.stepCount < 2 )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_invalid_dimension_steps" ),
                           QStringLiteral( "dimension %1 requires at least two ladder values" )
                               .arg( dim.parameterPath ) ) );
        if ( paths.contains( dim.parameterPath ) )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_duplicate_dimension" ),
                           QStringLiteral( "duplicate dimension %1" ).arg( dim.parameterPath ) ) );
        paths.insert( dim.parameterPath );
    }

    if ( metricNames.isEmpty() )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_no_metrics" ),
                       QStringLiteral( "a study requires at least one metric to aggregate" ) ) );
    QSet<QString> metrics;
    for ( const QString &metric : metricNames )
    {
        if ( metric.isEmpty() )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_no_metrics" ),
                           QStringLiteral( "metric names must be non-empty" ) ) );
        if ( metrics.contains( metric ) )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_duplicate_metric" ),
                           QStringLiteral( "duplicate metric %1" ).arg( metric ) ) );
        metrics.insert( metric );
    }
    for ( const StudyMetricSpec &objective : objectiveMetrics )
    {
        if ( !metrics.contains( objective.name ) )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_unknown_metric" ),
                           QStringLiteral( "objective metric %1 is not an aggregated metric" )
                               .arg( objective.name ) ) );
    }
    if ( !objectiveMetric.isEmpty() && !metrics.contains( objectiveMetric ) )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_unknown_metric" ),
                       QStringLiteral( "declared task metric %1 is not an aggregated metric" )
                           .arg( objectiveMetric ) ) );

    if ( !objectiveMetric.isEmpty() )
    {
        const bool declared =
            std::any_of( objectiveMetrics.cbegin(), objectiveMetrics.cend(),
                         [&]( const StudyMetricSpec &objective ) {
                             return objective.name == objectiveMetric;
                         } );
        if ( !declared )
            return Result<void>::failure(
                specError( QStringLiteral( "study.spec_missing_direction" ),
                           QStringLiteral( "declared task metric %1 requires an "
                                            "objective_metrics entry with its direction" )
                               .arg( objectiveMetric ) ) );
    }

    if ( spatialEpsilon < 0.0 || !std::isfinite( spatialEpsilon ) )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_invalid_epsilon" ),
                       QStringLiteral( "spatial epsilon must be a finite value >= 0" ) ) );

    const StudyBudget &b = budget;
    if ( b.maxRuns < 1 || b.maxInFlight < 1 || b.maxInFlight > kStudyMaxInFlightBound
         || b.seedReplicates < 1 || b.perRunTimeoutMs < 1 )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_invalid_budget" ),
                       QStringLiteral( "budget requires maxRuns >= 1, 1 <= maxInFlight <= %1, "
                                        "seedReplicates >= 1 and perRunTimeoutMs >= 1" )
                           .arg( kStudyMaxInFlightBound ) ) );
    if ( b.maxRuns > experiment::kMaxMatrixCells )
        return Result<void>::failure(
            specError( QStringLiteral( "study.spec_budget_over_cap" ),
                       QStringLiteral( "maxRuns %1 exceeds the sweep cap %2 — narrow the study, "
                                        "it will not be truncated" )
                           .arg( b.maxRuns )
                           .arg( experiment::kMaxMatrixCells ) ) );
    return Result<void>::success();
}

QJsonObject ParameterStudySpec::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kStudySpecSchemaVersion );
    json.insert( QStringLiteral( "study_id" ), studyId );
    json.insert( QStringLiteral( "experiment_id" ), experimentId );
    json.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    json.insert( QStringLiteral( "base_parameters" ), baseParameters );

    QJsonObject sampling;
    sampling.insert( QStringLiteral( "strategy" ), samplingStrategyToString( strategy ) );
    json.insert( QStringLiteral( "sampling" ), sampling );

    QJsonArray dims;
    for ( const ParameterDimension &dim : dimensions )
    {
        QJsonObject d;
        d.insert( QStringLiteral( "parameter_path" ), dim.parameterPath );
        d.insert( QStringLiteral( "min_value" ), dim.minValue );
        d.insert( QStringLiteral( "max_value" ), dim.maxValue );
        d.insert( QStringLiteral( "step_count" ), static_cast<double>( dim.stepCount ) );
        dims.append( d );
    }
    json.insert( QStringLiteral( "dimensions" ), dims );

    QJsonObject budgetJson;
    budgetJson.insert( QStringLiteral( "max_runs" ), static_cast<double>( budget.maxRuns ) );
    budgetJson.insert( QStringLiteral( "max_in_flight" ), static_cast<double>( budget.maxInFlight ) );
    budgetJson.insert( QStringLiteral( "per_run_timeout_ms" ),
                       static_cast<double>( budget.perRunTimeoutMs ) );
    budgetJson.insert( QStringLiteral( "seed_replicates" ),
                       static_cast<double>( budget.seedReplicates ) );
    budgetJson.insert( QStringLiteral( "seed" ), static_cast<double>( budget.seed ) );
    json.insert( QStringLiteral( "budget" ), budgetJson );

    QJsonArray metricsJson;
    for ( const QString &metric : metricNames )
        metricsJson.append( metric );
    json.insert( QStringLiteral( "metrics" ), metricsJson );

    QJsonArray objectiveJson;
    for ( const StudyMetricSpec &objective : objectiveMetrics )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "name" ), objective.name );
        o.insert( QStringLiteral( "maximize" ), objective.maximize );
        objectiveJson.append( o );
    }
    json.insert( QStringLiteral( "objective_metrics" ), objectiveJson );
    json.insert( QStringLiteral( "objective_metric" ), objectiveMetric );
    json.insert( QStringLiteral( "spatial_comparison" ), spatialComparison );
    json.insert( QStringLiteral( "spatial_epsilon" ), spatialEpsilon );
    return json;
}

Result<ParameterStudySpec> ParameterStudySpec::fromJson( const QJsonObject &json )
{
    if ( !json.contains( QStringLiteral( "schema_version" ) )
         || json.value( QStringLiteral( "schema_version" ) ).toInt() != kStudySpecSchemaVersion )
        return Result<ParameterStudySpec>::failure(
            specError( QStringLiteral( "study.spec_unsupported_version" ),
                       QStringLiteral( "study spec requires schema_version %1" )
                           .arg( kStudySpecSchemaVersion ) ) );

    // A specification is load-bearing: a key the reader does not know could
    // silently change the study's meaning, so it is refused instead of skipped.
    static const QSet<QString> kAllowedKeys = {
        QStringLiteral( "schema_version" ), QStringLiteral( "study_id" ),
        QStringLiteral( "experiment_id" ),  QStringLiteral( "algorithm_id" ),
        QStringLiteral( "base_parameters" ), QStringLiteral( "sampling" ),
        QStringLiteral( "dimensions" ),     QStringLiteral( "budget" ),
        QStringLiteral( "metrics" ),        QStringLiteral( "objective_metrics" ),
        QStringLiteral( "objective_metric" ), QStringLiteral( "spatial_comparison" ),
        QStringLiteral( "spatial_epsilon" ),
    };
    for ( auto it = json.constBegin(); it != json.constEnd(); ++it )
    {
        if ( !kAllowedKeys.contains( it.key() ) )
            return Result<ParameterStudySpec>::failure(
                specError( QStringLiteral( "study.spec_unknown_field" ),
                           QStringLiteral( "unknown study spec field %1" ).arg( it.key() ) ) );
    }

    ParameterStudySpec spec;
    spec.studyId = json.value( QStringLiteral( "study_id" ) ).toString();
    spec.experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    spec.algorithmId = json.value( QStringLiteral( "algorithm_id" ) ).toString();
    spec.baseParameters = json.value( QStringLiteral( "base_parameters" ) ).toObject();

    const QString strategyText =
        json.value( QStringLiteral( "sampling" ) ).toObject().value( QStringLiteral( "strategy" ) ).toString();
    const auto strategy = samplingStrategyFromString( strategyText );
    if ( !strategy )
        return Result<ParameterStudySpec>::failure(
            specError( QStringLiteral( "study.spec_invalid_strategy" ),
                       QStringLiteral( "unknown sampling strategy %1" ).arg( strategyText ) ) );
    spec.strategy = *strategy;

    const QJsonArray dims = json.value( QStringLiteral( "dimensions" ) ).toArray();
    for ( const auto &value : dims )
    {
        const QJsonObject d = value.toObject();
        if ( d.isEmpty() )
            return Result<ParameterStudySpec>::failure(
                specError( QStringLiteral( "study.spec_invalid_dimension" ),
                           QStringLiteral( "every dimension must be an object" ) ) );
        ParameterDimension dim;
        dim.parameterPath = d.value( QStringLiteral( "parameter_path" ) ).toString();
        dim.minValue = d.value( QStringLiteral( "min_value" ) ).toDouble();
        dim.maxValue = d.value( QStringLiteral( "max_value" ) ).toDouble();
        dim.stepCount = d.value( QStringLiteral( "step_count" ) ).toInt();
        spec.dimensions.append( dim );
    }

    const QJsonObject budgetJson = json.value( QStringLiteral( "budget" ) ).toObject();
    if ( !budgetJson.isEmpty() )
    {
        spec.budget.maxRuns = budgetJson.value( QStringLiteral( "max_runs" ) ).toInt();
        spec.budget.maxInFlight = budgetJson.value( QStringLiteral( "max_in_flight" ) ).toInt();
        spec.budget.perRunTimeoutMs =
            budgetJson.value( QStringLiteral( "per_run_timeout_ms" ) ).toInt();
        spec.budget.seedReplicates =
            budgetJson.value( QStringLiteral( "seed_replicates" ) ).toInt();
        spec.budget.seed = static_cast<quint64>(
            budgetJson.value( QStringLiteral( "seed" ) ).toDouble( 0.0 ) );
    }

    const QJsonArray metrics = json.value( QStringLiteral( "metrics" ) ).toArray();
    for ( const auto &metric : metrics )
        spec.metricNames.append( metric.toString() );

    const QJsonArray objectiveMetrics = json.value( QStringLiteral( "objective_metrics" ) ).toArray();
    for ( const auto &entry : objectiveMetrics )
    {
        const QJsonObject o = entry.toObject();
        StudyMetricSpec metricSpec;
        metricSpec.name = o.value( QStringLiteral( "name" ) ).toString();
        metricSpec.maximize = o.value( QStringLiteral( "maximize" ) ).toBool( true );
        spec.objectiveMetrics.append( metricSpec );
    }
    spec.objectiveMetric = json.value( QStringLiteral( "objective_metric" ) ).toString();
    spec.spatialComparison = json.value( QStringLiteral( "spatial_comparison" ) ).toBool( false );
    spec.spatialEpsilon = json.value( QStringLiteral( "spatial_epsilon" ) ).toDouble( 0.0 );

    const auto validation = spec.validate();
    if ( !validation )
        return Result<ParameterStudySpec>::failure( validation.diagnostics() );
    return Result<ParameterStudySpec>::success( spec );
}

} // namespace sicnu::study
