// studio_export.h — unified Studio export (Product G). Offline-reloadable.
#pragma once

#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

struct StudioExportBundle
{
    QString schema = QString::fromUtf8( kStudioExportSchema );
    QString studyId;
    QStringList runIds;
    QJsonObject studyReport;       ///< sicnu.studyreport.v1
    QJsonObject faultTeaching;     ///< fault teaching VM
    QJsonObject firstDivergence;   ///< first divergence VM
    QJsonObject designer;          ///< optional designer VM
    QJsonObject runMatrix;         ///< optional matrix VM
    /// R30-T07 (optional, additive): identity of the design that was
    /// actually submitted. Absent in bundles exported before R30-T07; the
    /// reader defaults it to an empty object so old documents still load.
    QJsonObject submission;
    QJsonObject metricCells;       ///< typed metric-cell identity per visible row
    QStringList capsuleRefs;
    QString csvRunTable;           ///< CSV text for run table
    QStringList issues;

    QJsonObject toJson() const;
    static Result<StudioExportBundle> fromJson( const QJsonObject &json );
};

QString studyRunTableToCsv( const QJsonObject &studyReportJson );

Result<void> writeStudioExportJson( const StudioExportBundle &bundle, const QString &path );
Result<StudioExportBundle> readStudioExportJson( const QString &path );

} // namespace sicnu::experiment_studio
