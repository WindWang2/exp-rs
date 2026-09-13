# GOAL — hyperspectral-spectral-intelligence-10 · Hyperspectral & Spectral Intelligence Platform 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> Track ID: `hyperspectral-spectral-intelligence-10`. Full goal brief as given
> in the track prompt; archived essentials below (the prompt is the authority;
> this file records the bound parameters and acceptance criteria).

* Branch `zcode/hyperspectral-spectral-intelligence-10`, worktree
  `../exp-rs-hyperspectral-spectral-intelligence-10`, off origin/master @
  `7d78059d1a6d316d606656759a506d17bc5e3b55`.
* Mission: converge the existing spectral stack (library, SAM/SID, MNF,
  PPI/endmember, unmixing, MF/ACE, RX) into a platform where structured
  spectral artifacts flow inside workflows, libraries are first-class
  operator inputs, MNF is a complete invertible chain, detection/unmixing are
  numerically guarded, and provenance/license rules are machine-checkable.
* Ownership: spectral/hyperspectral algorithms, spectral artifact/library
  integration, related operators/tests. Model Runtime inference = Track 08.
* Non-negotiables: master read-only; ≤2 read-only subagents; -j1/-j2 builds;
  no online CI dependency; no PR merge; P0/P1 review findings zero; final
  evidence at final HEAD.

## Track-specific acceptance (from the brief)

1. Hyperspectral workflow passes typed spectral artifacts (PPI/endmember →
   SAM/SID/unmixing as one workflow).
2. Library usable as operator first-class input (URI/id/version, validation,
   material filters, wavelength overlap, resampling, refusal, provenance,
   measured-vs-synthetic labeling).
3. Inverse MNF usable with known-answer tests.
4. Key detection/unmixing algorithms numerically guarded.
5. Spectral data provenance/license rules machine-verifiable.

Plus the generic completion gate (worktree off latest origin/master, dedupe
analysis, tracked planning files, platform-scale deliverable, subagent cap,
-j1/-j2, no CI wait, P0/P1 zero, final-HEAD verification, PR created not
merged).
