// src/recipes/recipe_compiler.h
#pragma once

//
// RS14-20 LabToRecipeCompiler.
//
// Deterministic, pure: same LabDocument + same IOperatorCatalog → identical
// recipe bytes. Everything not compilable lands in `diagnostics` (typed
// codes, see recipe_diagnostics.h) — never silently dropped.
//
// The compiler does NOT execute operators, resolve data paths, or touch the
// Processing Registry; operator knowledge arrives via IOperatorCatalog.
//

#include "recipes/lab_document.h"
#include "recipes/recipe_diagnostics.h"

#include <json/json.h>

#include <string>

namespace sicnu::recipes {

class IOperatorCatalog;

/// Compile output: the recipe document (always emitted, even with errors —
/// consumers decide via validator/severity) plus the full diagnostic list.
struct CompileResult
{
  Json::Value recipe{Json::objectValue};
  RecipeDiagnostics diagnostics;

  bool ok() const { return !hasErrors( diagnostics ); }
};

/// Compile one normalized lab document into a ScientificRecipe.
/// `ops` provides existence/param-name evidence (use FakeOperatorCatalog in
/// tests, SidecarOperatorCatalog for the shipped sidecar set).
CompileResult compileLabToRecipe( const LabDocument &lab, const IOperatorCatalog &ops );

} // namespace sicnu::recipes
