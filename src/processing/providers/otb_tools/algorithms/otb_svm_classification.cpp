// src/processing/providers/otb_tools/algorithms/otb_svm_classification.cpp
#include "otb_svm_classification.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

void OtbSvmClassificationAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", QObject::tr("Input raster")));

    addParameter(new QgsProcessingParameterVectorLayer("VECTOR", QObject::tr("Training vector data")));

    auto statsParam = new QgsProcessingParameterFile(
        "STATS", QObject::tr("Image statistics file (optional)"),
        Qgis::ProcessingFileParameterBehavior::File,
        "XML files (*.xml)", QVariant(), true);
    statsParam->setFlags(statsParam->flags() | Qgis::ProcessingParameterFlag::Optional);
    addParameter(statsParam);

    addParameter(new QgsProcessingParameterString("LABEL_FIELD", QObject::tr("Class label field"), QVariant("Class")));

    QStringList kernels;
    kernels << "linear" << "rbf" << "poly" << "sigmoid";
    addParameter(new QgsProcessingParameterEnum("KERNEL", QObject::tr("SVM kernel type"), kernels, false, 0));

    addParameter(new QgsProcessingParameterNumber(
        "C", QObject::tr("Cost parameter C"),
        Qgis::ProcessingNumberParameterType::Double, 1.0, false, 0.0));

    addParameter(new QgsProcessingParameterFileDestination("OUTPUT", QObject::tr("Output model file"),
                                                             "Model files (*.txt *.xml)"));
}

QStringList OtbSvmClassificationAlgorithm::buildArgs(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-io.il" << rasterLayerSource(parameters.value("INPUT"));
    args << "-io.vd" << vectorLayerSource(parameters.value("VECTOR"));
    args << "-sample.vfn" << parameters.value("LABEL_FIELD").toString();

    const QString statsPath = parameters.value("STATS").toString();
    if (!statsPath.isEmpty()) {
        args << "-io.imstat" << statsPath;
    }

    QStringList kernels = {"linear", "rbf", "poly", "sigmoid"};
    const QString kernel = kernels.value(parameters.value("KERNEL").toInt(), "linear");
    args << "-classifier" << "libsvm";
    args << "-classifier.libsvm.k" << kernel;
    args << "-classifier.libsvm.c" << QString::number(parameters.value("C").toDouble(), 'f', 4);
    args << "-io.out" << parameters.value("OUTPUT").toString();

    return args;
}