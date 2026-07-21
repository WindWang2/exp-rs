// src/workflow/builtin_definitions.h
#pragma once

namespace sicnu::workflow {

class WorkflowRegistry;

/// Register catalog of built-in single-step TaskPanel tools.
void registerBuiltinWorkflows( WorkflowRegistry &reg );

} // namespace sicnu::workflow
