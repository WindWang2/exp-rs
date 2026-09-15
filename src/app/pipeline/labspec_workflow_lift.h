// src/app/pipeline/labspec_workflow_lift.h — LabSpec 1.0 -> WorkflowDocument 2.0 (D17)
#pragma once

//
// The lift used by BOTH the guided workbench (cards view) and the E2E lab
// runner: operator-bound steps become lab-step nodes wired in document
// order; UI-verb / manual steps attach their guidance to the nearest
// operator step (forward when none seen yet). Lab projections land in
// WorkflowDocument.metadata["labSteps"][nodeId].
//

#include "../widgets/lab_spec_loader.h"
#include "workflow/workflow_ir_v2.h"

namespace sicnu::app::pipeline {

sicnu::workflow::WorkflowDocument liftLabSpecToWorkflow( const lab::LabSpec &spec );

} // namespace sicnu::app::pipeline
