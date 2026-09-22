# Tests — `test_science_context_broker`

Scenarios (Catch2, `Sicnu::ScienceContext`, sdk lane):

1. optical DN → NDVI needs calibration
2. SR → NDVI direct
3. missing band role blocking
4. CRS/grid conflict
5. SAR vs optical-only recipe
6. deterministic recipe order
7. unknown operator
8. registry/asset invalidation
9. autonomy L2 no autonomous exec
10. offline
11. truncation metadata
12. over-budget byte trim
13. hostile JSON/schema
14. tool parity (context/capabilities/passport/recipe)
15. non-ASCII path hint
16. byte-stable bundle
17. conflicted radiometric never auto-picked
18. DoD path passport → planner projection
19. tool hostile args errors
