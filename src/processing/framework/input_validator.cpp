// input_validator.cpp — Input validation implementation
#include "input_validator.h"

#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <QFileInfo>

bool InputValidator::validateRasterLayer(QgsRasterLayer *layer, QString &error)
{
    if (!layer) {
        error = QObject::tr("Raster layer is null");
        return false;
    }
    if (!layer->isValid()) {
        error = QObject::tr("Raster layer is not valid");
        return false;
    }
    return true;
}

bool InputValidator::validateRasterDimensions(int width, int height, QString &error)
{
    if (width <= 0) {
        error = QObject::tr("Raster width (%1) must be > 0").arg(width);
        return false;
    }
    if (height <= 0) {
        error = QObject::tr("Raster height (%1) must be > 0").arg(height);
        return false;
    }
    return true;
}

bool InputValidator::validateBandIndex(int band, int maxBands, QString &error)
{
    if (band < 1) {
        error = QObject::tr("Band index (%1) must be >= 1").arg(band);
        return false;
    }
    if (band > maxBands) {
        error = QObject::tr("Band index (%1) exceeds maximum (%2)").arg(band).arg(maxBands);
        return false;
    }
    return true;
}

bool InputValidator::validateOutputPath(const QString &path, QString &error)
{
    if (path.isEmpty()) {
        error = QObject::tr("Output path is empty");
        return false;
    }
    QFileInfo fi(path);
    QString parentDir = fi.absolutePath();
    if (!QFileInfo::exists(parentDir)) {
        error = QObject::tr("Parent directory does not exist: %1").arg(parentDir);
        return false;
    }
    return true;
}

bool InputValidator::validateNumericRange(double value, double min, double max, QString &error)
{
    if (std::isnan(value)) {
        error = QObject::tr("Value is NaN");
        return false;
    }
    if (std::isinf(value)) {
        error = QObject::tr("Value is infinite");
        return false;
    }
    if (value < min || value > max) {
        error = QObject::tr("Value %1 out of range [%2, %3]").arg(value).arg(min).arg(max);
        return false;
    }
    return true;
}

bool InputValidator::validateVectorLayer(QgsVectorLayer *layer, QString &error)
{
    if (!layer) {
        error = QObject::tr("Vector layer is null");
        return false;
    }
    if (!layer->isValid()) {
        error = QObject::tr("Vector layer is not valid");
        return false;
    }
    return true;
}

bool InputValidator::validateKernelSize(int kernelSize, QString &error)
{
    if (kernelSize < 1) {
        error = QObject::tr("Kernel size (%1) must be >= 1").arg(kernelSize);
        return false;
    }
    if (kernelSize % 2 == 0) {
        error = QObject::tr("Kernel size (%1) must be odd").arg(kernelSize);
        return false;
    }
    return true;
}
