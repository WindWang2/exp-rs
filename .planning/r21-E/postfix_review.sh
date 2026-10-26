#!/usr/bin/env bash
# R21-E post-chain fixes (R1 review findings) + rebuild + rerun.
set -uo pipefail
WT=/home/kevin/project/exp-rs-r21-E-20261023T215154Z
cd "$WT"
export QT_QPA_PLATFORM=offscreen
export CMAKE_BUILD_PARALLEL_LEVEL=4
LOG="$WT/.planning/r21-E/postfix.log"
: > "$LOG"

log() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }

log "applying R1a P3 fix: kernel-walk artifact-class refusal attaches verifier evidence"
python3 - << 'PYEOF' || exit 1
p='src/agent/output_verifier.cpp'
s=open(p).read()
old="""        if ( !runLabKernelWalks( *reader, kernelSpecs, QFileInfo( rules.path ).absolutePath(),
                                 options.maxBytes, kernelOutcomes, &kernelUsage, &ctx.error ) )
        {
            result.error = ctx.error;
            result.errorClass = kernelUsage ? QStringLiteral( "usage" )
                                            : QStringLiteral( "artifact" );
            return result;
        }"""
new="""        if ( !runLabKernelWalks( *reader, kernelSpecs, QFileInfo( rules.path ).absolutePath(),
                                 options.maxBytes, kernelOutcomes, &kernelUsage, &ctx.error ) )
        {
            result.error = ctx.error;
            result.errorClass = kernelUsage ? QStringLiteral( "usage" )
                                            : QStringLiteral( "artifact" );
            if ( !kernelUsage )
                attachVerifierEvidence( result, rules );
            return result;
        }"""
assert s.count(old)==1, "kernel-walk pattern not found"
s=s.replace(old,new)
open(p,'w').write(s)
print("kernel-walk attach added")
PYEOF

log "reconfigure + rebuild affected targets"
cmake -S . -B build-r21-E \
  -DNETCDF_INCLUDE_DIR=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/include \
  -DNETCDF_LIBRARY=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu/libnetcdf.so >> "$LOG" 2>&1 || exit 1
flock -x /tmp/exp-rs-r21-build.lock -c "cmake --build build-r21-E --target test_r21_e_verifier_evidence test_r21_e_faultlab_verifier test_r21_e_studio_verifier_hook test_r21_e_batch_stress -j4" >> "$LOG" 2>&1 || { log "FATAL: rebuild failed"; exit 1; }

log "rerun all four R21-E lanes (post-fix)"
for t in test_r21_e_verifier_evidence test_r21_e_faultlab_verifier test_r21_e_studio_verifier_hook test_r21_e_batch_stress; do
  "./build-r21-E/tests/$t" > "$WT/.planning/r21-E/POSTFIX_${t}.txt" 2>&1
  log "  $t exit=$?"
done

log "build CLI + tools for e2e"
flock -x /tmp/exp-rs-r21-build.lock -c "cmake --build build-r21-E --target sicnu_geo_rs_cli sicnu_generate_samples -j4" >> "$LOG" 2>&1 \
  && log "  CLI+tools OK" || log "  CLI+tools build failed (non-fatal for lanes)"

log "POSTFIX DONE"
