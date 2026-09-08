# Label schema & ontology

`LabelSchema` (versioned) + `LabelClass` rows: **stable UUID**, unique
string `code`, display name, description, parent code (hierarchy, acyclic),
`#rrggbb` color, background/ignore/unknown flags, aliases, metadata,
optional `legacyIntId` for RsClassDef interop (an alias, never identity).

Validation enforces unique codes/stable ids, resolvable parents, acyclic
hierarchies, at most one background. `ancestorsOf/descendantsOf/
leafCodes` support taxonomy reasoning.

`LabelMapping` recodes between schemas as an explicit named+versioned
operation: `covers()` lists unmapped leaf classes (background/ignore/
unknown may stay unmapped); `map()` never guesses. Applying a mapping is a
dataset-version event recorded in provenance - implicit re-encoding is
forbidden (ADR 0135).

Schemas are immutable per (id, version) in the store: identical re-save is
idempotent; differing re-save is `dataset.conflict`. A new vocabulary is a
new version.
