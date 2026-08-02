/***************************************************************************
 * rs_segmentation_utils.cpp  —  ADR 0060: only the grid-superpixel fallback
 * remains (the quantize/merge/write/rasterize helpers were deleted — see
 * the header).
 ***************************************************************************/
#include "rs_segmentation_utils.h"

namespace sicnu::operators::rs::segutil {

cv::Mat segmentGrid(int width, int height, int cellSize, RSOperatorContext& context) {
    if (cellSize < 4)
        cellSize = 4;
    context.reportProgress(0.18, "Segmenting: grid superpixels cell=" + std::to_string(cellSize));
    cv::Mat labels(height, width, CV_32S);
    const int cellsX = (width + cellSize - 1) / cellSize;
    for (int y = 0; y < height; ++y) {
        int* row = labels.ptr<int>(y);
        const int cy = y / cellSize;
        for (int x = 0; x < width; ++x) {
            const int cx = x / cellSize;
            row[x] = cy * cellsX + cx + 1;
        }
    }
    return labels;
}

} // namespace sicnu::operators::rs::segutil
