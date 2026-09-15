// tests/support/d15_e2e_pipeline.h — D15 Package I test-side orchestration.
//
// Wires the D15 public seams (spatial split -> features -> RF classify ->
// morphology -> CVA -> accuracy) into one headless pipeline over GDAL
// GeoTIFF inputs.  This is deliberately test-domain glue (namespace
// rs::testing); production seams stay in src/.
#pragma once

#include <string>

namespace rs::testing
{

struct E2ePipelineConfig
{
    std::string t1ImagePath;
    std::string t2ImagePath;
    std::string groundTruthMaskPath;   // class ids; 0 = masked/unclassifiable
    std::string outputClassificationPath;
    std::string outputChangeMapPath;
    /// Optional T2 ground truth; when set, the change Dice compares the
    /// detected mask against the true class transitions (T1 -> T2).
    /// Empty: the change oracle falls back to a large-margin spectral
    /// difference threshold.
    std::string t2GroundTruthMaskPath;
};

class ClassificationChangeE2ePipeline
{
  public:
    /// Runs the full chain and reports the classification accuracy of the
    /// post-processed class map and the change-detection Dice score.
    /// Returns false on unreadable inputs / I/O failures.
    static bool runFullWorkflow( const E2ePipelineConfig &config,
                                 double &outOverallAccuracy,
                                 double &outKappa,
                                 double &outChangeDice );
};

} // namespace rs::testing
