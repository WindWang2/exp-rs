#include "rubric_builder.h"

#include <QJsonArray>
#include <cmath>

namespace sicnu::teaching_admin {

QSet<QString> supportedLabRuleKinds()
{
    return {
        QStringLiteral( "range" ),
        QStringLiteral( "mean_sigma" ),
        QStringLiteral( "gain_invariance" ),
        QStringLiteral( "nodata_ratio" ),
        QStringLiteral( "histogram_shape" ),
        QStringLiteral( "classification_kappa" ),
        QStringLiteral( "confusion_marginals" ),
        QStringLiteral( "change_area_interval" ),
        QStringLiteral( "crs_grid" ),
        QStringLiteral( "zone_stats" ),
        QStringLiteral( "band_layout" ),
        QStringLiteral( "spatial_agreement" ),
        QStringLiteral( "series_separation" ),
        QStringLiteral( "spectral_signature" ),
        QStringLiteral( "file_check" ),
        QStringLiteral( "provenance" ),
    };
}

QSet<QString> supportedProcessCriterionKinds()
{
    return {
        QStringLiteral( "stage" ),
        QStringLiteral( "metric" ),
        QStringLiteral( "fact" ),
        QStringLiteral( "answer" ),
    };
}

WeightSumCheck checkWeightSum( const QJsonArray &weightedItems, const QString &weightKey, double expected,
                               double eps )
{
    WeightSumCheck c;
    c.expected = expected;
    for ( const auto &v : weightedItems )
        c.sum += v.toObject().value( weightKey ).toDouble( 0.0 );
    c.matchesExpected = std::fabs( c.sum - expected ) <= eps;
    return c;
}

ValidationResult validateLabRules( const QJsonObject &rules )
{
    ValidationResult r;
    if ( rules.value( QStringLiteral( "schema_version" ) ).toString() != QLatin1String( "sicnu.lab.rules/1" ) )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema_version" ),
                    QStringLiteral( "must be sicnu.lab.rules/1" ) );
    if ( rules.value( QStringLiteral( "lab_id" ) ).toString().isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "lab_id" ), QStringLiteral( "required" ) );
    if ( rules.value( QStringLiteral( "title" ) ).toString().isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "title" ), QStringLiteral( "required" ) );

    const QJsonObject artifact = rules.value( QStringLiteral( "artifact" ) ).toObject();
    const QString kind = artifact.value( QStringLiteral( "kind" ) ).toString();
    if ( kind != QLatin1String( "raster" ) && kind != QLatin1String( "file" ) )
        r.addError( QStringLiteral( "malformed_grading_rule" ), QStringLiteral( "artifact.kind" ),
                    QStringLiteral( "artifact.kind must be raster|file" ) );

    const QJsonArray assertions = rules.value( QStringLiteral( "assertions" ) ).toArray();
    if ( assertions.isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "assertions" ),
                    QStringLiteral( "assertions must be non-empty" ) );

    const QSet<QString> kinds = supportedLabRuleKinds();
    QSet<QString> ids;
    double weightSum = 0.0;
    for ( int i = 0; i < assertions.size(); ++i )
    {
        const QJsonObject a = assertions.at( i ).toObject();
        const QString path = QStringLiteral( "assertions[%1]" ).arg( i );
        const QString id = a.value( QStringLiteral( "id" ) ).toString();
        if ( id.isEmpty() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "assertion id required" ) );
        if ( ids.contains( id ) )
            r.addError( QStringLiteral( "duplicate_assertion_id" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "duplicate assertion id" ) );
        ids.insert( id );
        const QString ak = a.value( QStringLiteral( "kind" ) ).toString();
        if ( !kinds.contains( ak ) )
            r.addError( QStringLiteral( "unsupported_assertion_kind" ), path + QStringLiteral( ".kind" ),
                        QStringLiteral( "kind not supported by grader: " ) + ak );
        if ( !a.contains( QStringLiteral( "params" ) ) || !a.value( QStringLiteral( "params" ) ).isObject() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".params" ),
                        QStringLiteral( "params object required" ) );
        if ( !a.contains( QStringLiteral( "weight" ) ) )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight required" ) );
        // Weight arithmetic (R16-C/T06 WP3): per-item weights must be
        // finite and non-negative. A negative deduction weight cancels
        // against a bloated partner and passes an aggregate-only check,
        // and the grader would compute scores from nonsense arithmetic.
        // Type first (round-1 review): toDouble(0.0) coerced a non-numeric
        // weight ("0.5x") to a legal 0.0 — the value must be a JSON number.
        const QJsonValue weightValue = a.value( QStringLiteral( "weight" ) );
        if ( !weightValue.isDouble() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must be a number" ) );
        const double weight = weightValue.toDouble( 0.0 );
        if ( !std::isfinite( weight ) || weight < 0.0 )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must be finite and non-negative" ) );
        weightSum += weight;
        // Evidence / metric vocabulary soft checks
        if ( a.contains( QStringLiteral( "evidence" ) )
             && a.value( QStringLiteral( "evidence" ) ).toString().isEmpty() )
            r.addWarning( QStringLiteral( "missing_evidence_ref" ), path + QStringLiteral( ".evidence" ),
                          QStringLiteral( "empty evidence ref" ) );
    }

    // Lab rules historically use weights that sum to ~100 or ~1; flag mismatch vs 100 as warning if far.
    // Non-finite sums (overflow to inf, or a NaN member) must be refused
    // too: "sum > 0" alone holds for +inf and is silently false-compared
    // for NaN.
    if ( assertions.size() > 0 && ( !std::isfinite( weightSum ) || weightSum <= 0.0 ) )
        r.addError( QStringLiteral( "weight_mismatch" ), QStringLiteral( "assertions" ),
                    std::isfinite( weightSum )
                      ? QStringLiteral( "assertion weights must sum to a positive total" )
                      : QStringLiteral( "assertion weights must sum to a finite total" ) );

    return r;
}

ValidationResult validateProcessRubric( const QJsonObject &rubric )
{
    ValidationResult r;
    if ( rubric.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( "sicnu.grader.rubric/1" ) )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "must be sicnu.grader.rubric/1" ) );

    const QJsonArray criteria = rubric.value( QStringLiteral( "criteria" ) ).toArray();
    if ( criteria.isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "criteria" ),
                    QStringLiteral( "criteria required" ) );

    const QSet<QString> kinds = supportedProcessCriterionKinds();
    QSet<QString> ids;
    double sum = 0.0;
    for ( int i = 0; i < criteria.size(); ++i )
    {
        const QJsonObject c = criteria.at( i ).toObject();
        const QString path = QStringLiteral( "criteria[%1]" ).arg( i );
        const QString id = c.value( QStringLiteral( "id" ) ).toString();
        if ( id.isEmpty() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "criterion id required" ) );
        if ( ids.contains( id ) )
            r.addError( QStringLiteral( "duplicate_criterion_id" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "duplicate criterion id" ) );
        ids.insert( id );
        const QString kind = c.value( QStringLiteral( "kind" ) ).toString().toLower();
        if ( !kinds.contains( kind ) )
            r.addError( QStringLiteral( "unsupported_criterion_kind" ), path + QStringLiteral( ".kind" ),
                        QStringLiteral( "only stage|metric|fact|answer supported" ) );
        // Weight arithmetic (R16-C/T06 WP3): same per-item rule as lab
        // assertions — finite and non-negative; a NaN member made the
        // aggregate |sum - 1.0| comparison silently false below. Type
        // first (round-1 review): a non-numeric weight must be refused,
        // not coerced to 0.0.
        const QJsonValue weightValue = c.value( QStringLiteral( "weight" ) );
        if ( !weightValue.isDouble() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must be a number" ) );
        const double weight = weightValue.toDouble( 0.0 );
        if ( !std::isfinite( weight ) || weight < 0.0 )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must be finite and non-negative" ) );
        sum += weight;

        if ( kind == QLatin1String( "metric" )
             && c.value( QStringLiteral( "metricKey" ) ).toString().isEmpty()
             && c.value( QStringLiteral( "metric_key" ) ).toString().isEmpty() )
            r.addError( QStringLiteral( "missing_metric" ), path,
                        QStringLiteral( "metric criterion requires metricKey" ) );
    }

    // !isfinite first: |NaN - 1.0| > eps is FALSE, so a NaN sum used to
    // pass this aggregate check unseen.
    if ( criteria.size() > 0 && ( !std::isfinite( sum ) || std::fabs( sum - 1.0 ) > 1e-6 ) )
        r.addError( QStringLiteral( "weight_mismatch" ), QStringLiteral( "criteria" ),
                    QStringLiteral( "criterion weights must sum to 1.0 (got " )
                        + QString::number( sum ) + QStringLiteral( ")" ) );

    // Unjudgeable: criteria with no matchable evidence kind / answer key
    for ( int i = 0; i < criteria.size(); ++i )
    {
        const QJsonObject c = criteria.at( i ).toObject();
        const QString kind = c.value( QStringLiteral( "kind" ) ).toString().toLower();
        if ( kind == QLatin1String( "answer" )
             && c.value( QStringLiteral( "answerKey" ) ).toString().isEmpty()
             && c.value( QStringLiteral( "answer_key" ) ).toString().isEmpty() )
            r.addWarning( QStringLiteral( "unjudgeable_rubric" ),
                          QStringLiteral( "criteria[%1]" ).arg( i ),
                          QStringLiteral( "answer criterion lacks answerKey" ) );
    }

    return r;
}

} // namespace sicnu::teaching_admin
