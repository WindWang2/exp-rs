// src/workflow/builtin_definitions.h
#pragma once

namespace sicnu::workflow {

class WorkflowRegistry;

/// Register catalog of built-in TaskPanel tools and Workspace labs
/// (lab.classify.supervised, lab.georef.image_to_map, lab.obia stub).
void registerBuiltinWorkflows( WorkflowRegistry &reg );

} // namespace sicnu::workflow
