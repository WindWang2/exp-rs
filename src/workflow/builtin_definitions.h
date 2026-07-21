// src/workflow/builtin_definitions.h
#pragma once

namespace sicnu::workflow {

class WorkflowRegistry;

/// Register catalog of built-in TaskPanel tools and Workspace labs
/// (including lab.classify.supervised and lab.georef.image_to_map).
void registerBuiltinWorkflows( WorkflowRegistry &reg );

} // namespace sicnu::workflow
