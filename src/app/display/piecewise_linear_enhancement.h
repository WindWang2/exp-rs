// piecewise_linear_enhancement.h — QgsContrastEnhancementFunction for piecewise LUT
#pragma once

#include "display_stretch_types.h"

#include <qgscontrastenhancementfunction.h>

#include <vector>

namespace rs::display {

/**
 * Maps input DN/reflectance → display 0..255 via sorted control points
 * with linear interpolation between segments (分段线性拉伸).
 */
class PiecewiseLinearEnhancement : public QgsContrastEnhancementFunction
{
public:
  PiecewiseLinearEnhancement( Qgis::DataType dataType,
                              double minimumValue,
                              double maximumValue,
                              std::vector<ControlPoint> points );

  QgsContrastEnhancementFunction *clone() const override;
  int enhance( double value ) override;
  bool isValueInDisplayableRange( double value ) override;

private:
  std::vector<ControlPoint> m_points;
};

} // namespace rs::display
