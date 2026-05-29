// src/processing/providers/otb_tools/provider.cpp
#include "provider.h"
#include "algorithms/otb_band_math.h"
#include "algorithms/otb_segmentation.h"
#include "algorithms/otb_extract_roi.h"

#include <QIcon>

OtbToolsProvider::OtbToolsProvider()
    : QgsProcessingProvider()
{
}

QIcon OtbToolsProvider::icon() const
{
    return QIcon::fromTheme("otb");
}

QgsProcessingProvider *OtbToolsProvider::clone() const
{
    return new OtbToolsProvider();
}

void OtbToolsProvider::loadAlgorithms()
{
    addAlgorithm(new OtbBandMathAlgorithm());
    addAlgorithm(new OtbSegmentationAlgorithm());
    addAlgorithm(new OtbExtractRoiAlgorithm());
}
