#include "experiment_studio/spatial_compare_projection.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QString spatialCompareModeToString( SpatialCompareMode mode )
{
    switch ( mode )
    {
    case SpatialCompareMode::SideBySide:
        return QStringLiteral( "side_by_side" );
    case SpatialCompareMode::Swipe:
        return QStringLiteral( "swipe" );
    case SpatialCompareMode::DiffSummary:
        return QStringLiteral( "diff_summary" );
    case SpatialCompareMode::ClassTransition:
        return QStringLiteral( "class_transition" );
    }
    return QStringLiteral( "diff_summary" );
}

QJsonObject SpatialCompareViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "left_point_id" ), leftPointId );
    o.insert( QStringLiteral( "right_point_id" ), rightPointId );
    o.insert( QStringLiteral( "left_path" ), leftPath );
    o.insert( QStringLiteral( "right_path" ), rightPath );
    o.insert( QStringLiteral( "mode" ), spatialCompareModeToString( mode ) );
    o.insert( QStringLiteral( "ok" ), ok );
    o.insert( QStringLiteral( "grid_mismatch" ), gridMismatch );
    o.insert( QStringLiteral( "refuse_code" ), refuseCode );
    o.insert( QStringLiteral( "refuse_message" ), refuseMessage );
    QJsonArray hints;
    for ( const QString &h : alignmentWorkflowHints )
        hints.append( h );
    o.insert( QStringLiteral( "alignment_workflow_hints" ), hints );
    o.insert( QStringLiteral( "summary" ), summary.toJson() );
    o.insert( QStringLiteral( "preview_sample_cap" ), previewSampleCap );
    o.insert( QStringLiteral( "preview_used_sampling" ), previewUsedSampling );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    return o;
}

SpatialCompareViewModel projectSpatialSummary( const QString &leftPointId,
                                               const QString &rightPointId,
                                               const QString &leftPath,
                                               const QString &rightPath,
                                               const sicnu::study::SpatialDifferenceSummary &summary,
                                               SpatialCompareMode mode,
                                               const StudioResourcePolicy &policy )
{
    SpatialCompareViewModel vm;
    vm.leftPointId = leftPointId;
    vm.rightPointId = rightPointId;
    vm.leftPath = leftPath;
    vm.rightPath = rightPath;
    vm.mode = mode;
    vm.ok = true;
    vm.summary = summary;
    vm.previewSampleCap = policy.spatialPreviewMaxSamples;
    if ( summary.totalPixels > policy.spatialPreviewMaxSamples )
        vm.previewUsedSampling = true;
    return vm;
}

SpatialCompareViewModel projectSpatialRefusal( const QString &leftPointId,
                                               const QString &rightPointId,
                                               const QString &leftPath,
                                               const QString &rightPath,
                                               const QString &code,
                                               const QString &message,
                                               SpatialCompareMode mode )
{
    SpatialCompareViewModel vm;
    vm.leftPointId = leftPointId;
    vm.rightPointId = rightPointId;
    vm.leftPath = leftPath;
    vm.rightPath = rightPath;
    vm.mode = mode;
    vm.ok = false;
    vm.refuseCode = code;
    vm.refuseMessage = message;
    vm.gridMismatch = code.contains( QStringLiteral( "spatial_mismatch" ) )
                      || code.contains( QStringLiteral( "grid_mismatch" ) );
    if ( vm.gridMismatch )
    {
        vm.alignmentWorkflowHints = {
            QStringLiteral( "Register/resample via geospatial registration workflow (explicit)" ),
            QStringLiteral( "Re-run both points on a shared grid definition" ),
            QStringLiteral( "Do NOT silently overlay — Studio refuses resample" ),
        };
    }
    vm.issues.append( code + QLatin1Char( ':' ) + message );
    return vm;
}

Result<SpatialCompareViewModel> compareSpatialOutputs(
    const QString &leftPointId,
    const QString &rightPointId,
    const QString &leftPath,
    const QString &rightPath,
    const sicnu::study::ISpatialDifferenceSummarizer &summarizer,
    SpatialCompareMode mode,
    const StudioResourcePolicy &policy )
{
    if ( leftPath.isEmpty() || rightPath.isEmpty() )
    {
        return Result<SpatialCompareViewModel>::success(
            projectSpatialRefusal( leftPointId, rightPointId, leftPath, rightPath,
                                   QStringLiteral( "experiment_studio.spatial_missing_path" ),
                                   QStringLiteral( "both output paths are required for spatial compare" ),
                                   mode ) );
    }
    const auto result = summarizer.summarize( leftPath, rightPath );
    if ( !result )
    {
        QString code = QStringLiteral( "experiment_studio.spatial_failed" );
        QString message = QStringLiteral( "spatial summarize failed" );
        for ( const auto &d : result.diagnostics() )
        {
            code = d.code;
            message = d.message;
            break;
        }
        return Result<SpatialCompareViewModel>::success(
            projectSpatialRefusal( leftPointId, rightPointId, leftPath, rightPath, code, message,
                                   mode ) );
    }
    return Result<SpatialCompareViewModel>::success(
        projectSpatialSummary( leftPointId, rightPointId, leftPath, rightPath, result.value(), mode,
                               policy ) );
}

} // namespace sicnu::experiment_studio
