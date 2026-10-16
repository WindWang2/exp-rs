# Track R13-L: Verifiable Oracles

| Oracle ID | Target Module | Defect Mechanism | Verification Method / Assertion | Expected Result |
|---|---|---|---|---|
| O-01 | `pipeline_editor_dock.cpp` | Symlink clobbering | Create symlink `target.json -> link.json`, save pipeline to `link.json` | `target.json` updated with new content, `link.json` remains a valid symlink pointing to `target.json` |
| O-02 | `pipeline_editor_dock.cpp` | Permission degradation | Create file with permissions `0644` (or custom), overwrite via `writePipelineDefinitionFile` | Permissions remain preserved (not degraded to `0600`) |
| O-03 | `experiment_studio_dock.cpp` | Matrix table memory blowup | Call `populateMatrixTable` with 5,000 points | Table item count is capped at 500 rows (`kMaxMatrixDisplayRows`), UI remains responsive |
| O-04 | `agent_ops_control_center_dock.cpp` | Detached thread leak & UAF | Rapidly start, cancel, and destroy dock | All threads joined cleanly, zero ASan / thread sanitizer warnings, zero leaks |
| O-05 | `teaching_admin_dock.cpp` | Detached thread leak & UAF | Start batch assessment, trigger cancel/finish, destroy dock | Worker thread joined cleanly in destructor, zero dangling dereferences |
| O-06 | `main_window_vector.cpp` | Dishonest cut feedback | Attempt cut on read-only / un-deletable features | Returns false/error, edit command rolled back, truthful feedback shown |
