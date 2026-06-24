// input_validator.h — Input validation framework
#pragma once

#include <QString>
#include <limits>

class QgsRasterLayer;
class QgsVectorLayer;

/**
 * Input validation framework for processing algorithms.
 * Provides static methods to validate common input types.
 *
 * Usage:
 *   QString error;
 *   if (!InputValidator::validateRasterLayer(layer, error)) {
 *       throw QgsProcessingException(error);
 *   }
 */
class InputValidator
{
public:
    /**
     * Validate a raster layer is non-null and valid.
     */
    static bool validateRasterLayer(QgsRasterLayer *layer, QString &error);

    /**
     * Validate raster dimensions are positive.
     */
    static bool validateRasterDimensions(int width, int height, QString &error);

    /**
     * Validate band index is in range [1, maxBands].
     */
    static bool validateBandIndex(int band, int maxBands, QString &error);

    /**
     * Validate output path is non-empty and writable.
     */
    static bool validateOutputPath(const QString &path, QString &error);

    /**
     * Validate numeric value is in range and finite.
     */
    static bool validateNumericRange(double value, double min, double max, QString &error);

    /**
     * Validate a vector layer is non-null and valid.
     */
    static bool validateVectorLayer(QgsVectorLayer *layer, QString &error);

    /**
     * Validate kernel size is odd and positive.
     */
    static bool validateKernelSize(int kernelSize, QString &error);
};
