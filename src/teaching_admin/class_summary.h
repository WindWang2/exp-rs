// class_summary.h — Class Summary stats (no cloud profiles / privacy analytics).
#pragma once

#include "batch_assessment.h"

#include <QJsonObject>

namespace sicnu::teaching_admin {

QJsonObject buildClassSummary( const BatchAssessmentReport &report );

} // namespace sicnu::teaching_admin
