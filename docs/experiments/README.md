# Experiment Foundation 5.0

`src/experiment` (library `Sicnu::experiment`) provides experiment/run
identity, typed metrics + evaluation protocols, comparability-first run
comparison, the joined lineage graph, allowlisted environment capture and
reproduction bundles (ADR 0137-0138).

An `ExperimentRun` RECORDS an execution performed through TaskCenter/
JobEngine/workflow - it never executes anything (no second scheduler).
It binds: dataset version + fingerprint, split manifest + fingerprint,
algorithm/workflow identity, canonical parameters, seed, model id+digest,
determinism grade, environment, artifacts, metrics.

CLI: `sicnu_geo_rs_cli experiment create|inspect|compare`,
`sicnu_geo_rs_cli reproduce export|validate`.
