# CAPABILITY_MATRIX — before/after

| capability | before (master a5b11b7f) | after (this track) | state |
|---|---|---|---|
| grading kernels | 9 statistical kernels, no position-sensitivity | + zone_stats, band_layout, series_separation, spectral_signature, spatial_agreement | implemented |
| executable rules coverage | 6 grading labs; labspec 8–11 intent-only; 4 labs ungraded | + rules for labspec 8–11 | implemented |
| data pack contract | data-specs for 4 labs, no checksums | sicnu.lab-pack/1 manifests + validator, all labs | implemented |
| batch grading | CSV only, no identity | roster, identity, dedupe, JSON/HTML summary, caps | implemented |
| lab report | GUI-only, grade never recorded | lab --report CLI + recorded grade embedding | implemented |
| copilot injection evals | role-claim only | dedicated injection/leak corpus | implemented |
| offline diagnostics | bundle scripts | lab --self-check typed diagnostics | implemented |
| classroom scale | none | bounded default + opt-in 1000 synthetic | implemented |
| (any not-supported/degraded rows appended during execution) | | | |
