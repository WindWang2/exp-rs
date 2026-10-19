# R18-C cross-track findings (NOT fixed here — ownership elsewhere)

## CT-1 (R18-D): cartography tool registrations missing `type:"object"` in inputSchema
- `src/agent/cartography/cartography_tools.cpp` `DiffTemplatesTool::inputSchema()` (~line 1606-1618, tool `cartography:diff_templates`): builds `{properties:{before,after}, required:[...]}` WITHOUT the root `schema["type"] = "object"`. Its own `outputSchema()` right below sets type — the input side just misses it.
- Symptom surfaced by R18-C C01's cross-surface parity gate: `tools/list`/`get_tool_schema` now emit the normalized schema (with `type`), while the registry raw copy lacks it. The projection-side unification is R18-C's fix; the raw registration should also add the root type so registry truth and projection are byte-identical.
- Suggested fix (R18-D, one line): `schema["type"] = "object";` at the top of that inputSchema() (and audit sibling tools in the same file for the same omission).

## CT-3 (unowned / src/agent root): data-platform paginated tools still silently rewind bad cursors
- `src/agent/data_platform_tools.cpp:133-137` `pageCursor()`: `toLongLong()` then clamp — a non-numeric cursor silently pages from 0 for every `dataset:`/`experiment:`/`reproducibility:` list tool, the same client-bug-hider class R18-C C01 fixed for `tools/list`/`list_algorithms`/`list_operators`/`list_tools`/`search_tools`/`search_algorithms`. Not fixed here: `src/agent/data_platform_tools.cpp` is outside this track's OWNERSHIP list. Suggested: route it through a typed INVALID_PARAMETER refusal like `mcpTypedCursor`.

## CT-2 (infra): repo-wide CI without completed runs since 2026-10-11 (issue #1491)
- All R18-C verification is local CTest + node --test under the five-track lock; CI cannot be waited on (per task rules).
