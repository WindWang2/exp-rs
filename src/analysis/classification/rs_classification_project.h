// rs_classification_project.h — classification project (.rscproj) persistence.
//
// JSON manifest storing workflow step/mode, result paths, and related flags.
// Missing keys load with backward-compatible defaults.
#pragma once

#include "qgis_analysis_export.h"

#include <QString>

/**
 * \brief Serializable classification project snapshot.
 *
 * Written as a JSON object (typically file extension \c .rscproj).
 * Unknown or missing fields keep the defaults below when loading.
 */
struct QGIS_ANALYSIS_EXPORT RsClassificationProjectData
{
    int version = 1;

    int workflowStep = 0;
    QString workflowMode; // "wizard" | "expert"

    QString sourceRasterPath;
    QString roisPath;
    QString classifiedRasterPath;
    QString postProcessRasterPath;
    QString postProcessVectorPath;

    bool evaluateReviewed = false;
    QString accuracySource; // "holdout" | "valid_layer"

    // Optional accuracy snapshot (-1 = unset).
    double overallAccuracy = -1.0;
    double kappa = -1.0;
};

/**
 * \brief Load/save \ref RsClassificationProjectData as JSON.
 */
class QGIS_ANALYSIS_EXPORT RsClassificationProject
{
  public:
    /// Write \a data to \a path. Returns true on success.
    static bool save( const QString &path, const RsClassificationProjectData &data );

    /// Read \a path into \a data. Missing keys keep defaults. Returns false
    /// if the file cannot be read or is not a JSON object.
    static bool load( const QString &path, RsClassificationProjectData &data );
};
