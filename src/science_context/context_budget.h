// src/science_context/context_budget.h
#pragma once
#include "science_context/bundle.h"

namespace sicnu::science_context {

struct BudgetPolicy
{
    int maxBytes = 65536;
    int maxRecipes = 5;
    int maxCapabilities = 8;
    int maxOpenQuestions = 12;
    int maxAssets = 8;
};

void applyContextBudget( ScientificContextBundle &bundle, const BudgetPolicy &policy );

} // namespace sicnu::science_context
