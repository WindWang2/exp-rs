# Deferred Items: Issue #728

The following items are identified as belonging to Issue #728 (Remote COG / STAC Large Data Source Refactor) and are intentionally **deferred** to prevent merge conflicts with parallel workflow/cache branches:

1. **STAC Query URL Canonicalization**:
   - Normalizing varying query param orders, stripping transient SAS/presigned tokens from asset URLs, and resolving canonical collection IDs across remote providers.
2. **Signed Query URLs & Lease Expiry**:
   - Refreshing expiring signed URLs during long multi-epoch workflow runs.
3. **GDAL Remote Block Optimization**:
   - Background pre-caching and chunked VSI range-request handling to prevent GUI thread blocking during remote raster header inspection.
4. **Date-Range Inclusivity / Boundaries**:
   - Edge-case datetime boundary alignment (inclusive vs exclusive RFC3339 timestamps) in remote STAC API searches.

*These items must NOT be modified in the `antigravity/temporal-agent-contract` worktree.*
