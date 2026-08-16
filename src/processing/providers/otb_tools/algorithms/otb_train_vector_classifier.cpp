// src/processing/providers/otb_tools/algorithms/otb_train_vector_classifier.cpp
#include "otb_train_vector_classifier.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void OtbTrainVectorClassifierAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterVectorLayer("INPUT", "Input vector (training data)"));
    addParameter(new QgsProcessingParameterString("FEATURES", "Feature field names (comma-separated)"));
    addParameter(new QgsProcessingParameterString("LABEL_FIELD", "Label field name"));
    addParameter(new QgsProcessingParameterFileDestination("OUTPUT", "Output model file",
                                                            "Model files (*.xml *.txt)"));
}

QStringList OtbTrainVectorClassifierAlgorithm::buildArgs(const QVariantMap &parameters,
                                                         QgsProcessingContext &context,
                                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-io.vd" << vectorLayerSource(parameters.value("INPUT"));
    const QString featStr = parameters.value("FEATURES").toString();
    if (!featStr.isEmpty()) {
        args << "-feat";
        for (const QString &f : featStr.split(',', Qt::SkipEmptyParts)) {
            args << f.trimmed();
        }
    }
    args << "-cfield" << parameters.value("LABEL_FIELD").toString();
    args << "-io.out" << parameters.value("OUTPUT").toString();

    return args;
}
