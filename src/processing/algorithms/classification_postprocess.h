// src/processing/algorithms/classification_postprocess.h — D15 Package D seam.
//
// Post-classification thematic cleanup: majority filtering, connected-
// component sieve and the clump-and-eliminate composite.  Inputs/outputs are
// label rasters (row-major ints); the conventional NoData label is -1 and
// never absorbs or merges into thematic classes.
#pragma once

#include <span>
#include <vector>

namespace rs::processing
{

struct MorphologicalFilterConfig
{
    int windowSize{ 3 };        // 3 or 5 (odd); even values snap to nearest odd
    int minSievePixelSize{ 4 }; // components smaller than this are eliminated
    int connectivity{ 8 };      // 4 or 8: any value other than 4 is treated as 8
    int noDataValue{ -1 };
};

class ClassificationPostProcessor
{
  public:
    /// Strict-majority filter: the centre pixel adopts the window majority
    /// only when some class holds MORE THAN half of the valid window cells;
    /// ties (and NoData-only windows) keep the centre value.  Borders use
    /// clamp replication.
    static std::vector<int> applyMajorityFilter( std::span<const int> inClassification,
                                                 int width,
                                                 int height,
                                                 const MorphologicalFilterConfig &config );

    /// Removes connected components (4/8-connectivity) whose area is smaller
    /// than @p minPixelSize by reassigning them to the adjacent component
    /// sharing the longest orthogonal border (ties -> lowest class id; a
    /// component fully enclosed by NoData becomes NoData).  This seam's
    /// NoData sentinel is the fixed label -1 (spec signature carries no
    /// config); shift labels as the D15 pipeline does when 0 means nodata.
    static std::vector<int> applySieveFilter( std::span<const int> inClassification,
                                              int width,
                                              int height,
                                              int minPixelSize,
                                              int connectivity );

    /// Named composite of clumping (component labelling) and elimination —
    /// same guarantees as applySieveFilter; kept as a distinct seam because
    /// pipelines refer to it semantically.
    static std::vector<int> clumpAndEliminate( std::span<const int> inClassification,
                                               int width,
                                               int height,
                                               int minPixelSize,
                                               int connectivity );
};

} // namespace rs::processing
