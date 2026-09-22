// run_matrix_projection.h — Run Matrix view model (Product B).
// Projects StudyReport / StudyRunRow; owns no thread pool.
#pragma once

#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"
#include "study/study_export.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment_studio
{

struct RunMatrixFilter
{
    QString statusEquals; ///< empty = any
    QString pointIdContains;
    QString runIdContains;
    bool onlyFailed = false;
};

struct RunMatrixViewModel
{
    QString schema = QString::fromUtf8( kRunMatrixVmSchema );
    QString studyId;
    QString experimentId;
    int totalPoints = 0;
    int recordedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    int missingCount = 0;
    QVector<sicnu::study::StudyRunRow> rows; ///< filtered projection
    QStringList selectedPointIds;            ///< up to two for compare
    QStringList issues;

    QJsonObject toJson() const;
};

RunMatrixViewModel projectRunMatrix( const sicnu::study::StudyReport &report,
                                     const RunMatrixFilter &filter = {} );

/// Deterministic point identity helper (delegates to matrix cell semantics).
QString pointIdentityKey( const sicnu::study::StudyRunRow &row );

} // namespace sicnu::experiment_studio
