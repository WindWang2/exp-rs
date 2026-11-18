# RESUME — R35-T03 (post-PR checkpoint)
- PR: https://github.com/WindWang2/exp-rs/pull/2151 (base master@120677e6, head d7b9e9f8f2)
- Branch: zcode/r35-t03-data-io-catalog-20261118T155406Z (worktree exp-rs-r35-t03-data-io-catalog-20261118T155406Z)
- Delivered WP01/02/04/05/10/11/16 (+WP03 documented no-change). WP06-09/12-15/17-20 remain track backlog:
  resume by reproducing each on the then-current HEAD per the 20-WP plan in the R35-T03 brief.
- Build dir: build-dev (Debug, dev-default + -DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5)
- Host lock: $HOME/.cache/exp-rs-agent/host-build (shared, exit 75 = wait and retry)
- Known pre-existing (NOT this track): test_r15_B_geodata_persistence r403 SSRF pair SIGABRTs order-dependently
  (proven at base by full revert rebuild; passes in isolation). Candidate for a remote-track fix.
